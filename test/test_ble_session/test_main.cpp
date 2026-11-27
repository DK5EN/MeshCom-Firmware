// BLC-01 (docs/concept-open-issues-20261004.md section 8): per-session BLE reset,
// disconnect classification and counters of src/ble_session.h.
//
// The regression proof is the edge race. The old ESP32 code reset isPhoneReady
// only on the loop-observed edge `!deviceConnected && oldDeviceConnected`. When
// a phone disconnects and the next central connects between two loop passes the
// loop sees no edge and the new central inherits isPhoneReady == 1 (skips the
// hello and the PIN check). `Sim` models both variants; legacy == true is the
// old behaviour. The race test for the new behaviour fails when it is run
// against the legacy variant (set RACE_USES_LEGACY to 1 to see that).
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "ble_session.h"

#ifndef RACE_USES_LEGACY
#define RACE_USES_LEGACY 0
#endif

BleStats g_bleStats;   // the platform defines it once; here the test does

struct Sim
{
    bool legacy;
    // the real globals of esp32_main.cpp / loop_functions.cpp
    volatile bool deviceConnected;
    bool oldDeviceConnected;
    int  isPhoneReady;
    bool uart, ack, prep, fin;
    BleStats st;
};

static Sim makeSim(bool legacy)
{
    Sim s;
    memset(&s, 0, sizeof(s));
    s.legacy = legacy;
    return s;
}

static BleSessionFlags flagsOf(Sim &s)
{
    BleSessionFlags f = { &s.isPhoneReady, &s.uart, &s.ack, &s.prep, &s.fin };
    return f;
}

// NimBLE host task
static void onConnect(Sim &s)
{
    if(s.legacy)
    {
        s.deviceConnected = true;
        s.prep = false;     // the old onConnect cleared only these two
        s.fin = false;
    }
    else
    {
        bleSessionReset(flagsOf(s));
        bleStatsOnConnect(s.st);
        s.deviceConnected = true;
    }
}

static void onDisconnect(Sim &s, int reason)
{
    s.deviceConnected = false;
    if(!s.legacy)
    {
        bleSessionReset(flagsOf(s));
        bleStatsOnDisconnect(s.st, reason);
    }
}

// one pass of esp32loop()
static void loopTick(Sim &s)
{
    if(s.deviceConnected)
        s.uart = true;

    if(!s.deviceConnected && s.oldDeviceConnected)
    {
        s.oldDeviceConnected = s.deviceConnected;
        if(s.legacy)
        {
            s.uart = false;
            s.isPhoneReady = 0;
            s.ack = false;
            s.prep = false;
            s.fin = false;
        }
        else if(!s.deviceConnected)
            s.uart = false;
    }
    if(s.deviceConnected && !s.oldDeviceConnected)
        s.oldDeviceConnected = s.deviceConnected;
}

// the phone sends hello: session is ready, config burst under way, ackinfo on
static void phoneHello(Sim &s)
{
    s.isPhoneReady = 1;
    s.ack = true;
    s.prep = true;
    s.fin = true;
}

void setUp(void) {}
void tearDown(void) {}

// ---- the edge race ---------------------------------------------------------

static void test_race_reconnect_within_one_tick_resets_session(void)
{
    Sim s = makeSim(RACE_USES_LEGACY != 0);

    onConnect(s);
    loopTick(s);                 // loop observes the connect
    phoneHello(s);
    TEST_ASSERT_EQUAL_INT(1, s.isPhoneReady);

    onDisconnect(s, 0x213);      // phone leaves ...
    onConnect(s);                // ... and the next central is in before the loop runs

    // the new central must start unauthenticated, before any loop pass
    TEST_ASSERT_EQUAL_INT(0, s.isPhoneReady);
    TEST_ASSERT_FALSE(s.uart);
    TEST_ASSERT_FALSE(s.ack);
    TEST_ASSERT_FALSE(s.prep);
    TEST_ASSERT_FALSE(s.fin);

    loopTick(s);                 // no edge is visible; nothing may come back
    TEST_ASSERT_EQUAL_INT(0, s.isPhoneReady);
    TEST_ASSERT_TRUE(s.uart);    // loop marks the new link as connected
}

static void test_legacy_edge_race_leaks_ready_flag(void)
{
    // characterization of the defect: the old logic keeps isPhoneReady == 1
    Sim s = makeSim(true);

    onConnect(s);
    loopTick(s);
    phoneHello(s);
    onDisconnect(s, 0x213);
    onConnect(s);
    loopTick(s);

    TEST_ASSERT_EQUAL_INT(1, s.isPhoneReady);   // the leak
    TEST_ASSERT_TRUE(s.ack);
}

static void test_legacy_edge_alone_is_fine_when_loop_sees_it(void)
{
    // with a loop pass between disconnect and connect the old logic worked
    Sim s = makeSim(true);
    onConnect(s);
    loopTick(s);
    phoneHello(s);
    onDisconnect(s, 0x213);
    loopTick(s);
    onConnect(s);
    loopTick(s);
    TEST_ASSERT_EQUAL_INT(0, s.isPhoneReady);
}

static void test_normal_cycle_with_loop_pass(void)
{
    Sim s = makeSim(false);
    onConnect(s);
    loopTick(s);
    phoneHello(s);
    onDisconnect(s, 0x208);
    TEST_ASSERT_EQUAL_INT(0, s.isPhoneReady);   // already clear before the loop pass
    loopTick(s);
    TEST_ASSERT_FALSE(s.uart);
    TEST_ASSERT_FALSE(s.oldDeviceConnected);
    onConnect(s);
    loopTick(s);
    TEST_ASSERT_EQUAL_INT(0, s.isPhoneReady);
    TEST_ASSERT_TRUE(s.uart);
    TEST_ASSERT_EQUAL_UINT32(2, s.st.con);
    TEST_ASSERT_EQUAL_UINT32(1, s.st.dis);
}

static void test_loop_edge_does_not_clear_flag_of_new_central(void)
{
    // the loop saw "down" late: a new central already connected and said hello;
    // the delayed edge must not wipe that session
    Sim s = makeSim(false);
    onConnect(s);
    loopTick(s);
    onDisconnect(s, 0x213);
    s.oldDeviceConnected = true;          // loop has not run yet
    onConnect(s);
    phoneHello(s);
    loopTick(s);                          // connected && old: no edge at all
    TEST_ASSERT_EQUAL_INT(1, s.isPhoneReady);
}

// ---- bleSessionReset -------------------------------------------------------

static void test_session_reset_clears_every_flag(void)
{
    int ready = 1; bool u = true, a = true, p = true, f = true, d = true;
    BleSessionFlags fl = { &ready, &u, &a, &p, &f, &d };
    bleSessionReset(fl);
    TEST_ASSERT_FALSE_MESSAGE(d, "wrong-PIN disconnect request leaks into the next session");
    TEST_ASSERT_EQUAL_INT(0, ready);
    TEST_ASSERT_FALSE(u);
    TEST_ASSERT_FALSE(a);
    TEST_ASSERT_FALSE(p);
    TEST_ASSERT_FALSE(f);
}

static void test_session_reset_skips_null_members(void)
{
    int ready = 1; bool a = true;
    BleSessionFlags fl = { &ready, nullptr, &a, nullptr, nullptr };
    bleSessionReset(fl);
    TEST_ASSERT_EQUAL_INT(0, ready);
    TEST_ASSERT_FALSE(a);
}

static void test_session_reset_keep_phone_only_resets_link(void)
{
    int ready = 1; bool u = true, a = true, p = true, f = true, d = true;
    BleSessionFlags fl = { &ready, &u, &a, &p, &f, &d };
    bleSessionReset(fl, true);
    TEST_ASSERT_FALSE(u);
    TEST_ASSERT_FALSE(d);
    TEST_ASSERT_EQUAL_INT(1, ready);
    TEST_ASSERT_TRUE(a);
    TEST_ASSERT_TRUE(p);
    TEST_ASSERT_TRUE(f);
}

static void test_session_reset_keep_phone_false_is_full_reset(void)
{
    int ready = 1; bool u = true, a = true, p = true, f = true, d = true;
    BleSessionFlags fl = { &ready, &u, &a, &p, &f, &d };
    bleSessionReset(fl, false);
    TEST_ASSERT_EQUAL_INT(0, ready);
    TEST_ASSERT_FALSE(u);
    TEST_ASSERT_FALSE(a);
    TEST_ASSERT_FALSE(p);
    TEST_ASSERT_FALSE(f);
    TEST_ASSERT_FALSE(d);
}

static void test_session_reset_keep_phone_skips_null_members(void)
{
    int ready = 1;
    BleSessionFlags fl = { &ready, nullptr, nullptr, nullptr, nullptr, nullptr };
    bleSessionReset(fl, true);
    TEST_ASSERT_EQUAL_INT(1, ready);
}

// ---- classification --------------------------------------------------------

static void test_classify_nimble_and_raw_hci_codes(void)
{
    BleStats s; memset(&s, 0, sizeof(s));
    bleStatsOnDisconnect(s, 0x208);   // NimBLE: 0x200 + HCI
    bleStatsOnDisconnect(s, 0x08);    // Bluefruit: raw HCI
    TEST_ASSERT_EQUAL_UINT32(2, s.to);
    bleStatsOnDisconnect(s, 0x213);
    bleStatsOnDisconnect(s, 0x13);
    TEST_ASSERT_EQUAL_UINT32(2, s.rem);
    bleStatsOnDisconnect(s, 0x216);
    bleStatsOnDisconnect(s, 0x16);
    TEST_ASSERT_EQUAL_UINT32(2, s.loc);
    bleStatsOnDisconnect(s, 0x23E);
    bleStatsOnDisconnect(s, 0x3E);
    TEST_ASSERT_EQUAL_UINT32(2, s.fail);
    bleStatsOnDisconnect(s, 0x222);   // LL response timeout: other
    bleStatsOnDisconnect(s, 0x100);   // low byte 0
    bleStatsOnDisconnect(s, 0x00);
    bleStatsOnDisconnect(s, 0x03);
    TEST_ASSERT_EQUAL_UINT32(4, s.other);
    TEST_ASSERT_EQUAL_UINT32(12, s.dis);
    TEST_ASSERT_EQUAL_UINT32(s.dis, s.to + s.rem + s.loc + s.fail + s.other);
    TEST_ASSERT_EQUAL_UINT32(0, s.con);
    TEST_ASSERT_EQUAL_UINT32(0, s.adv);
}

static void test_classify_does_not_confuse_0x208_with_other_codes(void)
{
    BleStats s; memset(&s, 0, sizeof(s));
    bleStatsOnDisconnect(s, 0x208);
    TEST_ASSERT_EQUAL_UINT32(1, s.to);
    TEST_ASSERT_EQUAL_UINT32(0, s.other);
    bleStatsOnDisconnect(s, 0x209);   // 0x09 is not 0x08
    TEST_ASSERT_EQUAL_UINT32(1, s.to);
    TEST_ASSERT_EQUAL_UINT32(1, s.other);
}

static void test_connect_and_adv_counters(void)
{
    BleStats s; memset(&s, 0, sizeof(s));
    bleStatsOnConnect(s); bleStatsOnConnect(s);
    bleStatsOnAdvRestart(s);
    TEST_ASSERT_EQUAL_UINT32(2, s.con);
    TEST_ASSERT_EQUAL_UINT32(1, s.adv);
    TEST_ASSERT_EQUAL_UINT32(0, s.dis);
}

// ---- text and format -------------------------------------------------------

static void test_reason_text_table(void)
{
    TEST_ASSERT_EQUAL_STRING("Supervision Timeout",      bleReasonText(0x208));
    TEST_ASSERT_EQUAL_STRING("Supervision Timeout",      bleReasonText(0x08));
    TEST_ASSERT_EQUAL_STRING("Remote User Terminated",   bleReasonText(0x213));
    TEST_ASSERT_EQUAL_STRING("Remote User Terminated",   bleReasonText(0x13));
    TEST_ASSERT_EQUAL_STRING("Local Host Terminated",    bleReasonText(0x216));
    TEST_ASSERT_EQUAL_STRING("LL Response Timeout",      bleReasonText(0x222));
    TEST_ASSERT_EQUAL_STRING("Unacceptable Conn Params", bleReasonText(0x23B));
    TEST_ASSERT_EQUAL_STRING("Failed to Establish",      bleReasonText(0x23E));
    TEST_ASSERT_EQUAL_STRING("Failed to Establish",      bleReasonText(0x3E));
    TEST_ASSERT_EQUAL_STRING("other",                    bleReasonText(0x205));
    TEST_ASSERT_EQUAL_STRING("other",                    bleReasonText(0));
}

static void test_format_exact(void)
{
    BleStats s = { 7, 6, 3, 2, 1, 0, 0, 5 };
    char buf[96];
    int n = bleStatsFormat(s, buf, sizeof(buf));
    const char *want = "BLE: con=7 dis=6 to=3 rem=2 loc=1 fail=0 adv=5";
    TEST_ASSERT_EQUAL_STRING(want, buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(want), n);
}

static void test_format_zero_and_max_fit_128(void)
{
    BleStats z; memset(&z, 0, sizeof(z));
    char buf[128];
    bleStatsFormat(z, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("BLE: con=0 dis=0 to=0 rem=0 loc=0 fail=0 adv=0", buf);

    BleStats m = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                   0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    int n = bleStatsFormat(m, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0 && n < 128);
    TEST_ASSERT_NOT_NULL(strstr(buf, "con=4294967295 dis=4294967295"));
}

static void test_format_truncates_safely(void)
{
    BleStats s = { 1, 2, 3, 4, 5, 6, 7, 8 };
    char buf[12];
    memset(buf, 'x', sizeof(buf));
    bleStatsFormat(s, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(11, (int)strlen(buf));   // NUL-terminated within n
}

// ---- phone frame queue: drop counting and session tag (BLE-QDROP-01, BLE-STALE-01) ----
// QSim mirrors the platform code: a bounded FIFO (xQueueCreate(5)), the producer and the loop.
// BLE_QUEUE_LEGACY=1 builds the code as it was before the fix (xQueueSend result ignored, no
// session check before readPhoneCommand): the cases below then fail, which is the red run.
#ifndef BLE_QUEUE_LEGACY
#define BLE_QUEUE_LEGACY 0
#endif

BleQueueCounters g_bleQ;   // the platform defines it once; here the test does

struct QSim
{
    BleQueueItem q[5];
    int n;
    int applied;        // frames that reached readPhoneCommand
    int appliedStale;   // ... of an earlier connection
    uint32_t sessionOfApplied[16];
};

static QSim g_q;
static uint32_t g_connection;   // which connection the "phone" is in (test truth, not the code's)

static void qReset(void)
{
    memset(&g_q, 0, sizeof(g_q));
    g_connection = 0;
    g_bleQ.gen.store(0); g_bleQ.dropped.store(0); g_bleQ.stale.store(0);
    g_bleQ.droppedLogged = 0; g_bleQ.staleLogged = 0;
}


static void qProduce(void)   // onWrite / bleuart_rx_callback
{
    BleQueueItem it = {};
    it.length = 1;
    it.data[0] = (uint8_t)g_connection;
    bleItemStamp(it, g_bleQ);
    bool ok = g_q.n < 5;
    if(ok) g_q.q[g_q.n++] = it;
#if !BLE_QUEUE_LEGACY
    bleQueueOnSend(g_bleQ, ok);
#endif
}

static void qConnectCb(void)    { g_connection++; bleQueueNewSession(g_bleQ); }
static void qDisconnectCb(void) { bleQueueNewSession(g_bleQ); }

// one item dequeued, the loop is "between dequeue and apply" when the connection changes
static BleQueueItem g_inflight;

static void qApply(const BleQueueItem &it)
{
#if !BLE_QUEUE_LEGACY
    if(!bleQueueAccept(g_bleQ, it)) return;
#endif
    if(g_q.applied < 16) g_q.sessionOfApplied[g_q.applied] = it.data[0];
    g_q.applied++;
    if(it.data[0] != g_connection) g_q.appliedStale++;
}

static void qLoopDrain(void)
{
    while(g_q.n > 0)
    {
        BleQueueItem it = g_q.q[0];
        memmove(&g_q.q[0], &g_q.q[1], sizeof(g_q.q[0]) * (size_t)(g_q.n - 1));
        g_q.n--;
        qApply(it);
    }
}

static void test_queue_full_drop_is_counted(void)
{
    qReset();
    for(int i = 0; i < 7; i++) qProduce();   // capacity 5: two frames lost
    TEST_ASSERT_EQUAL_INT(5, g_q.n);
    TEST_ASSERT_EQUAL_UINT32(2, g_bleQ.dropped.load());
    TEST_ASSERT_EQUAL_UINT32(2, bleQueueTakeDropped(g_bleQ));
    TEST_ASSERT_EQUAL_UINT32(0, bleQueueTakeDropped(g_bleQ));   // logged once per pass
    qProduce();
    TEST_ASSERT_EQUAL_UINT32(1, bleQueueTakeDropped(g_bleQ));   // only the delta
}

static void test_queued_item_of_old_connection_not_applied(void)
{
    qReset();
    qConnectCb();            // connection 1
    qProduce();              // hello of the old central, still queued
    qDisconnectCb();
    qConnectCb();            // connection 2, loop has not run in between
    qLoopDrain();
    TEST_ASSERT_EQUAL_INT(0, g_q.appliedStale);
    TEST_ASSERT_EQUAL_INT(0, g_q.applied);
    TEST_ASSERT_EQUAL_UINT32(1, bleQueueTakeStale(g_bleQ));
    TEST_ASSERT_EQUAL_UINT32(0, bleQueueTakeStale(g_bleQ));
}

static void test_inflight_item_of_old_connection_not_applied(void)
{
    // a queue reset in the callbacks cannot catch this one: already dequeued
    qReset();
    qConnectCb();
    qProduce();
    g_inflight = g_q.q[0]; g_q.n = 0;   // loop dequeued it ...
    qDisconnectCb();                    // ... the host task reconnects meanwhile
    qConnectCb();
    qApply(g_inflight);                 // ... and the loop applies it now
    TEST_ASSERT_EQUAL_INT(0, g_q.appliedStale);
    TEST_ASSERT_EQUAL_UINT32(1, g_bleQ.stale.load());
}

static void test_new_connection_items_still_applied(void)
{
    qReset();
    qConnectCb(); qProduce(); qDisconnectCb(); qConnectCb();
    qProduce();                          // own hello of the new central
    qLoopDrain();
    TEST_ASSERT_EQUAL_INT(1, g_q.applied);
    TEST_ASSERT_EQUAL_UINT8(2, g_q.sessionOfApplied[0]);
}

static void test_item_helpers(void)
{
    BleQueueItem it = {};
    BleQueueCounters c;
    c.gen.store(5);
    bleItemStamp(it, c);
    TEST_ASSERT_EQUAL_UINT32(5, it.gen);
    TEST_ASSERT_TRUE(bleItemIsCurrent(it, 5));
    TEST_ASSERT_FALSE(bleItemIsCurrent(it, 6));
    c.gen.store(0xFFFFFFFFu);            // wrap: only equality counts
    bleItemStamp(it, c);
    bleQueueNewSession(c);
    TEST_ASSERT_EQUAL_UINT32(0, c.gen.load());
    TEST_ASSERT_FALSE(bleItemIsCurrent(it, c.gen.load()));
}

static void test_queue_format(void)
{
    BleQueueCounters c;
    c.dropped.store(3); c.stale.store(4);
    char buf[64];
    bleQueueFormat(c, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("BLE: qdrop=3 qstale=4", buf);
}


int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_race_reconnect_within_one_tick_resets_session);
    RUN_TEST(test_legacy_edge_race_leaks_ready_flag);
    RUN_TEST(test_legacy_edge_alone_is_fine_when_loop_sees_it);
    RUN_TEST(test_normal_cycle_with_loop_pass);
    RUN_TEST(test_loop_edge_does_not_clear_flag_of_new_central);
    RUN_TEST(test_session_reset_clears_every_flag);
    RUN_TEST(test_session_reset_skips_null_members);
    RUN_TEST(test_classify_nimble_and_raw_hci_codes);
    RUN_TEST(test_classify_does_not_confuse_0x208_with_other_codes);
    RUN_TEST(test_session_reset_keep_phone_only_resets_link);
    RUN_TEST(test_session_reset_keep_phone_false_is_full_reset);
    RUN_TEST(test_session_reset_keep_phone_skips_null_members);
    RUN_TEST(test_connect_and_adv_counters);
    RUN_TEST(test_reason_text_table);
    RUN_TEST(test_format_exact);
    RUN_TEST(test_format_zero_and_max_fit_128);
    RUN_TEST(test_format_truncates_safely);
    RUN_TEST(test_queue_full_drop_is_counted);
    RUN_TEST(test_queued_item_of_old_connection_not_applied);
    RUN_TEST(test_inflight_item_of_old_connection_not_applied);
    RUN_TEST(test_new_connection_items_still_applied);
    RUN_TEST(test_item_helpers);
    RUN_TEST(test_queue_format);
    return UNITY_END();
}
