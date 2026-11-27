// test_ack_rx_core: ackRxDecide() (src/ack_rx_core.h, FORMAL-02 wave 1) against the former decision logic of
// handleACK() (ack_rx_ref.h, frozen). Both run against a copy of the two tables handleACK reads (dedup ring,
// own_msg_id[]) and record every side effect as an event; sequences and the final tables must be equal.
// The "new" side is runNew() below: it performs the verdict the way handleACK() in lora_functions.cpp does,
// with the same recorders instead of the firmware calls (lora_functions.cpp itself is not linkable natively).
// Inputs: ack_corpus.txt, the 0x41 frames of the other corpora, every truncation of those, generated valid
// ACKs with random ids/appendix/state, raw noise at every size 0..255. 2026-11-27.
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#include <string>

#include <ack_rx_core.h>
#include "../support/repo_file.h"
#include "ack_rx_ref.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t g_s = 0x5EED1234u;
static uint32_t rnd(void)
{
    g_s = g_s * 1664525u + 1013904223u;
    return g_s >> 8;
}

// --- the shell: what handleACK() does with the verdict, same order, recorders instead of firmware calls ---
static bool runNew(RefEnv &e, const uint8_t *payload, uint16_t size)
{
    AckRxVerdict v = ackRxDecide(payload, size, e.dedup, e.own, e.gwId, e.ackInfo, e.meshOn);

    if(v.action == ACK_RX_NOT_ACK)
    {
        refEv(e, EV_RETURN_FALSE, 1);
        return false;
    }
    if(v.action == ACK_RX_IMPLAUSIBLE)
    {
        refEv(e, EV_DBG_REJECT, payload[5], size);
        refEv(e, EV_RETURN_FALSE, 2);
        return false;
    }

    uint8_t print_buff[ACK_WIRE_MAX_LEN];
    memcpy(print_buff, v.frame, v.frameLen);

    if(v.n == ACK_WIRE_APPENDIX_LEN)
        refEv(e, EV_DBG_APPENDIX, v.appendixHash, size);
    refEv(e, EV_DBG_DEDUP, v.rxId, (uint32_t)v.dedupSlot);
    refEv(e, EV_DEDUP_COUNT, v.isNew);
    refEv(e, EV_DISPLAY_LOG, !v.isNew, 0, payload, size);

    if(v.ownSlot >= 0)
        refEv(e, EV_CHECKOWN_LOG, v.ackedId, (uint32_t)v.ownSlot);
    if(v.lateAck)
        refEv(e, EV_LATE_STATUS, v.ackedId);

    if(v.addRx)
    {
        refEv(e, EV_ADD_RX, v.rxId, v.serverFlag);
        refAddRx(e, v.rxId, v.serverFlag);
    }

    if(v.ownSlot >= 0)
    {
        if(v.phoneStatus)
        {
            if(v.phoneFrame)
            {
                refEv(e, EV_PHONE_FRAME, v.ackedId);
                refEv(e, EV_GW_ACK_COUNT);
                refEv(e, EV_PHONE_LOG, v.ackedId);
            }
            refEv(e, EV_SET_STATUS_ACK, (uint32_t)v.ownSlot);
            e.own[v.ownSlot][4] = 0x02;
        }
        if(v.stopRing)
        {
            int ackSlot = refStopRing(v.ackedId);
            refEv(e, EV_STOP_RING, v.ackedId, (uint32_t)ackSlot);
            if(ackSlot >= 0)
                refEv(e, EV_RETX_LOG, ackSlot, v.ackedId);
        }
    }
    else if(v.fwdCandidate)
    {
        refEv(e, EV_SERVER_RX_ASK, 0, 0, print_buff + 6, 4);
        if(!refCheckServerRx(e, print_buff + 6))
        {
            print_buff[5]--;
            refEv(e, EV_FWD_TX, 0, 0, print_buff, v.frameLen);
            refEv(e, EV_FWD_LOG);
        }
    }

    refEv(e, EV_NEWLINE);
    refEv(e, EV_RETURN_TRUE);
    return true;
}

// --- bookkeeping ---
static unsigned long g_cmp = 0, g_evKind[32], g_accept = 0, g_notAck = 0, g_impl = 0, g_app = 0;

static bool sameEv(const Ev &a, const Ev &b)
{
    return a.kind == b.kind && a.a == b.a && a.b == b.b && a.len == b.len && memcmp(a.buf, b.buf, sizeof(a.buf)) == 0;
}

static void compareOne(const RefEnv &base, const uint8_t *buf, uint16_t size, const char *what)
{
    // the buffer is a full-size heap copy with a 0xEE tail so a read past size is visible to RD() only
    std::vector<uint8_t> p(buf, buf + size);
    RefEnv a = base, b = base;
    a.ev.clear();
    b.ev.clear();
    a.oob = b.oob = false;

    uint8_t pad[1] = {0xEE};
    const uint8_t *pa = size ? p.data() : pad;
    bool ra = ackRxRef(a, pa, size);
    bool rb = runNew(b, size ? p.data() : NULL, size);

    char msg[200];
    g_cmp++;
    snprintf(msg, sizeof(msg), "%s size=%u return", what, (unsigned)size);
    TEST_ASSERT_EQUAL_MESSAGE(ra, rb, msg);
    snprintf(msg, sizeof(msg), "%s size=%u event count %u vs %u", what, (unsigned)size, (unsigned)a.ev.size(), (unsigned)b.ev.size());
    TEST_ASSERT_EQUAL_MESSAGE(a.ev.size(), b.ev.size(), msg);
    for(size_t i = 0; i < a.ev.size(); i++)
    {
        snprintf(msg, sizeof(msg), "%s size=%u event %u kind %d vs %d", what, (unsigned)size, (unsigned)i, a.ev[i].kind, b.ev[i].kind);
        TEST_ASSERT_TRUE_MESSAGE(sameEv(a.ev[i], b.ev[i]), msg);
    }
    snprintf(msg, sizeof(msg), "%s size=%u tables", what, (unsigned)size);
    TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(a.dedup, b.dedup, sizeof(a.dedup)), msg);
    TEST_ASSERT_EQUAL_MESSAGE(0, memcmp(a.own, b.own, sizeof(a.own)), msg);
    TEST_ASSERT_EQUAL_UINT8(a.loraWrite, b.loraWrite);

    // the former code read payload[0] before checking size (size 0); nothing else may read past size
    snprintf(msg, sizeof(msg), "%s size=%u former code out-of-range read", what, (unsigned)size);
    TEST_ASSERT_EQUAL_MESSAGE(size == 0, a.oob, msg);

    for(size_t i = 0; i < a.ev.size(); i++)
        g_evKind[a.ev[i].kind]++;
    if(ra)
    {
        g_accept++;
        if(b.ev.size() && size >= 15 && p[11] == 3) g_app++;
    }
    else if(size >= 12 && size && p[0] == MSG_TYPE_ACK) g_impl++;
    else g_notAck++;
}

// --- state generation: plant what the frame will hit, so every branch is reached ---
static void fillEnv(RefEnv &e, const uint8_t *f, uint16_t size)
{
    memset(e.dedup, 0, sizeof(e.dedup));
    memset(e.own, 0, sizeof(e.own));
    e.ev.clear();
    e.oob = false;
    e.gwId = rnd() & 0x3FFFFF;
    e.ackInfo = rnd() & 1;
    e.meshOn = (rnd() % 4) != 0;
    for(int i = 0; i < MAX_DEDUP_RING; i++)
    {
        uint32_t id = rnd() * 2654435761u;
        memcpy(e.dedup[i], &id, 4);
        e.dedup[i][4] = rnd() & 1;
    }
    e.loraWrite = rnd() % MAX_DEDUP_RING;
    for(int i = 0; i < MAX_RING; i++)
    {
        uint32_t id = (e.gwId << 10) | (rnd() & 0x3FF);
        if(rnd() % 3 == 0) id = rnd() * 2246822519u;
        memcpy(e.own[i], &id, 4);
        e.own[i][4] = rnd() % 5;
    }
    if(size < 10)
        return;
    uint32_t acked = (uint32_t)f[6] | ((uint32_t)f[7] << 8) | ((uint32_t)f[8] << 16) | ((uint32_t)f[9] << 24);
    switch(rnd() % 6)
    {
    case 0: memcpy(e.dedup[rnd() % MAX_DEDUP_RING], f + 1, 4); e.dedup[rnd() % MAX_DEDUP_RING][4] = rnd() & 1; break;     // dup
    case 1: memcpy(e.dedup[rnd() % MAX_DEDUP_RING], f + 6, 4); break;                                                       // acked id seen
    case 2: { int s = rnd() % MAX_DEDUP_RING; memcpy(e.dedup[s], f + 6, 4); e.dedup[s][4] = 1; } break;                    // seen from a server
    default: break;
    }
    switch(rnd() % 7)
    {
    case 0: memcpy(e.own[rnd() % MAX_RING], f + 6, 4); break;                                                               // own, verbatim
    case 1: e.gwId = acked >> 10; { uint32_t o = pnRetryId(acked, e.gwId, 0); memcpy(e.own[rnd() % MAX_RING], &o, 4); } break; // retry copy folded
    case 2: e.gwId = acked >> 10; memcpy(e.own[rnd() % MAX_RING], f + 6, 4); break;                                         // own node id
    case 3: { uint32_t z = 0; memcpy(e.own[rnd() % MAX_RING], &z, 4); } break;                                              // empty slot vs id 0
    default: break;
    }
}

static void checkAllStates(const uint8_t *f, uint16_t size, const char *what, int envs)
{
    for(int k = 0; k < envs; k++)
    {
        RefEnv e;
        fillEnv(e, f, size);
        compareOne(e, f, size, what);
    }
}

static int hex2(const char *h)
{
    auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1)); };
    int a = nib(h[0]), b = nib(h[1]);
    return (a < 0 || b < 0) ? -1 : (a << 4 | b);
}

// every frame of a corpus file ("<name> <hex>"), returns how many started with 0x41
static unsigned long corpusRun(const char *rel, int envs, bool truncate)
{
    FILE *fp = repo_fopen(rel, "r");
    char miss[160];
    snprintf(miss, sizeof(miss), "%s not found", rel);
    TEST_ASSERT_NOT_NULL_MESSAGE(fp, miss);
    char line[2400];
    unsigned long n41 = 0;
    while(fgets(line, sizeof(line), fp))
    {
        if(line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char name[32], hex[2200];
        if(sscanf(line, "%31s %2199s", name, hex) != 2) continue;
        uint8_t f[1100];
        int n = 0;
        for(const char *h = hex; h[0] && h[1] && n < (int)sizeof(f); h += 2)
        {
            int v = hex2(h);
            if(v < 0) break;
            f[n++] = (uint8_t)v;
        }
        if(n == 0) continue;
        if(f[0] == MSG_TYPE_ACK) n41++;
        if(!truncate && f[0] != MSG_TYPE_ACK) continue;
        if(!truncate)
        {
            checkAllStates(f, (uint16_t)n, name, envs);
            continue;
        }
        // only the 0x41 frames and a sample of others get every truncation (bounded runtime)
        if(f[0] != MSG_TYPE_ACK && (n41 % 7) != 0) continue;
        int top = n > 40 ? 40 : n;
        for(int len = 0; len <= top; len++)
            checkAllStates(f, (uint16_t)len, name, 1);
    }
    fclose(fp);
    return n41;
}

static void test_ack_corpus(void)
{
    unsigned long n = corpusRun("test/test_aprs_fuzz/ack_corpus.txt", 12, false);
    TEST_ASSERT_GREATER_OR_EQUAL(500, n);
    corpusRun("test/test_aprs_fuzz/ack_corpus.txt", 1, true);
    TEST_ASSERT_GREATER_THAN(6000, g_cmp);
}

static void test_other_corpora_0x41(void)
{
    unsigned long n = 0;
    n += corpusRun("test/test_aprs_fuzz/capture_corpus.txt", 6, false);
    n += corpusRun("test/test_aprs_corpus/corpus.txt", 6, false);
    corpusRun("test/test_aprs_fuzz/capture_corpus.txt", 1, true);
    corpusRun("test/test_aprs_corpus/corpus.txt", 1, true);
    TEST_ASSERT_GREATER_OR_EQUAL(4, n);   // the three capture frames and the corpus.txt frame start with 0x41
}

static void test_generated_valid_acks(void)
{
    for(int i = 0; i < 30000; i++)
    {
        uint8_t f[24];
        for(size_t j = 0; j < sizeof(f); j++) f[j] = rnd();
        f[0] = MSG_TYPE_ACK;
        uint32_t r = rnd() % 10;
        f[5] = (r < 8) ? (uint8_t)(0x80 | (rnd() % (MAX_HOP_LIMIT + 1))) : (uint8_t)rnd();   // 0..MAX_HOP valid, else random
        if(rnd() % 8 == 0) f[5] = 0x80;                                                       // zero hops left
        f[10] = rnd() % 8 ? 1 : (uint8_t)rnd();
        f[11] = rnd() % 3 == 0 ? 3 : (rnd() % 2 ? 0 : (uint8_t)rnd());
        if(rnd() % 12 == 0) { f[6] = f[7] = f[8] = f[9] = 0; }                                // acked id 0
        if(rnd() % 12 == 0) memcpy(f + 6, f + 1, 4);                                          // acked id == own id
        uint16_t size = (uint16_t)(12 + rnd() % 13);
        checkAllStates(f, size, "gen", 1);
    }
}

static void test_noise_every_size(void)
{
    for(int size = 0; size <= 255; size++)
    {
        for(int k = 0; k < 120; k++)
        {
            uint8_t f[256];
            for(int j = 0; j < 256; j++) f[j] = rnd();
            if(k % 2) f[0] = MSG_TYPE_ACK;
            if(k % 4 == 1) f[5] = 0x80 | (rnd() % 8);
            checkAllStates(f, (uint16_t)size, "noise", 1);
        }
    }
}

// handleACK() read payload[0] before it looked at size. A zero-length frame must not touch the buffer.
// The buffer here is a PROT_NONE page: reading it would fault, the core must return NOT_ACK.
static void test_size_zero_never_reads(void)
{
    long pg = sysconf(_SC_PAGESIZE);
    void *m = mmap(NULL, pg, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    TEST_ASSERT_TRUE(m != MAP_FAILED);
    RefEnv e;
    uint8_t dummy[12] = {0};
    fillEnv(e, dummy, 0);
    AckRxVerdict v = ackRxDecide((const uint8_t *)m, 0, e.dedup, e.own, e.gwId, e.ackInfo, e.meshOn);
    TEST_ASSERT_EQUAL(ACK_RX_NOT_ACK, v.action);
    // evidence for the old logic: it flags the index-0 read at size 0
    RefEnv a = e;
    a.ev.clear();
    a.oob = false;
    uint8_t one = 0x00;
    ackRxRef(a, &one, 0);
    TEST_ASSERT_TRUE_MESSAGE(a.oob, "former handleACK read payload[0] at size 0");
    munmap(m, pg);
}

static void test_coverage_floor(void)
{
    printf("\n[ack_rx] %lu frames compared (%lu accepted, %lu implausible, %lu not ACK)\n", g_cmp, g_accept, g_impl, g_notAck);
    // the earlier tests ran first (Unity order); every decision handleACK makes must have been compared
    TEST_ASSERT_GREATER_THAN(60000, g_cmp);
    TEST_ASSERT_GREATER_THAN(5000, g_accept);
    TEST_ASSERT_GREATER_THAN(1000, g_impl);
    TEST_ASSERT_GREATER_THAN(1000, g_notAck);
    TEST_ASSERT_GREATER_THAN(100, g_app);
    const int need[] = {EV_RETURN_FALSE, EV_DBG_REJECT, EV_DBG_APPENDIX, EV_DBG_DEDUP, EV_DEDUP_COUNT, EV_DISPLAY_LOG,
                        EV_CHECKOWN_LOG, EV_LATE_STATUS, EV_ADD_RX, EV_PHONE_FRAME, EV_GW_ACK_COUNT, EV_PHONE_LOG,
                        EV_SET_STATUS_ACK, EV_STOP_RING, EV_RETX_LOG, EV_SERVER_RX_ASK, EV_FWD_TX, EV_FWD_LOG, EV_NEWLINE,
                        EV_RETURN_TRUE};
    for(size_t i = 0; i < sizeof(need) / sizeof(need[0]); i++)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "event kind %d never compared", need[i]);
        TEST_ASSERT_GREATER_THAN_MESSAGE(50, g_evKind[need[i]], msg);
    }
    // a server-flagged id must have blocked a forward, and a dup with an own slot must have been handled
    TEST_ASSERT_GREATER_THAN(50, g_evKind[EV_SERVER_RX_ASK] - g_evKind[EV_FWD_TX]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_size_zero_never_reads);
    RUN_TEST(test_ack_corpus);
    RUN_TEST(test_other_corpora_0x41);
    RUN_TEST(test_generated_valid_acks);
    RUN_TEST(test_noise_every_size);
    RUN_TEST(test_coverage_floor);
    return UNITY_END();
}
