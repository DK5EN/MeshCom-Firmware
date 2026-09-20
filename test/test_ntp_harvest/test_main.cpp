// TM-45: since TM-35, an NTP reply is only ever picked up by the gateway's
// receive path (getMeshComUDP() on ESP32, NrfETH::getUDP() on nRF52), which
// itself only runs while bGATEWAY is on. A non-gateway node transmitted its
// request fine and then always timed out -- the reply sat unread in the
// socket (0x [NTP];ok, ~545x [NTP];timeout per board in a 9.1 h soak).
//
// ntpHarvestReply() (ntp_async.h) is the harvest-only substitute for that
// path, called from the bGATEWAY-off branch on both platforms. This tests
// the template directly against a mock socket -- the same shape of proof
// test_ntp_async already gives tryConsume() itself, extended to the socket
// read this fix adds.
#include <unity.h>

#include "Arduino.h"
#include "ntp_async.h"

// ------------------------------------------------------------------ Fake-UDP
//
// Same shape as test_ntp_async's FakeUdp (derives from the abstract UDP the
// NtpAsync constructor needs), extended with the read-side primitives
// ntpHarvestReply() calls (parsePacket/read/remoteIP/remotePort). These are
// plain methods, not virtuals on the shared UDP stub (test/support/Udp.h) --
// ntpHarvestReply() is a template, so it binds directly against FakeUdp's
// own methods instead of going through UDP's vtable, exactly as it binds
// against WiFiUDP/EthernetUDP in production without either needing to widen
// that shared interface.
struct FakeUdp : public UDP
{
    uint16_t boundPort = 0;
    int      packetsSent = 0;
    IPAddress lastDest;
    uint16_t lastDestPort = 0;
    uint8_t  lastPayload[64] = {0};
    size_t   lastLen = 0;
    bool     failSend = false;

    // read side: one queued datagram at a time, like a real UDP socket
    bool      hasQueued = false;
    IPAddress queuedIp;
    uint16_t  queuedPort = 0;
    uint8_t   queuedBuf[300] = {0};
    int       queuedLen = 0;
    int       parsePacketCalls = 0;
    int       readCalls = 0;

    uint8_t begin(uint16_t port) override { boundPort = port; return 1; }

    int beginPacket(IPAddress ip, uint16_t port) override
    {
        if(failSend)
            return 0;
        lastDest = ip;
        lastDestPort = port;
        lastLen = 0;
        return 1;
    }

    size_t write(const uint8_t *buffer, size_t size) override
    {
        size_t n = size < sizeof(lastPayload) ? size : sizeof(lastPayload);
        memcpy(lastPayload, buffer, n);
        lastLen = size;
        return size;
    }

    int endPacket() override { packetsSent++; return 1; }

    // queue a datagram for the next parsePacket()/read() pair to pick up
    void queue(IPAddress ip, uint16_t port, const uint8_t *buf, int len)
    {
        hasQueued = true;
        queuedIp = ip;
        queuedPort = port;
        int n = len < (int)sizeof(queuedBuf) ? len : (int)sizeof(queuedBuf);
        memcpy(queuedBuf, buf, n);
        queuedLen = n;
    }

    int parsePacket()
    {
        parsePacketCalls++;
        return hasQueued ? queuedLen : 0;
    }

    int read(uint8_t *buf, int len)
    {
        readCalls++;
        if(!hasQueued)
            return 0;
        int n = queuedLen < len ? queuedLen : len;
        memcpy(buf, queuedBuf, n);
        hasQueued = false;     // one datagram consumed, same as a real socket
        return n;
    }

    IPAddress remoteIP() { return queuedIp; }
    uint16_t  remotePort() { return queuedPort; }
};

static const IPAddress kServer(162, 159, 200, 1);

// NTP reply with transmit timestamp == secs (seconds since 1900)
static void makeReply(uint8_t *pkt, unsigned long secsSince1900, uint8_t stratum = 2, uint8_t mode = 4)
{
    memset(pkt, 0, NTP_ASYNC_PACKET_SIZE);
    pkt[0] = (uint8_t)((0 << 6) | (4 << 3) | mode);
    pkt[1] = stratum;
    pkt[40] = (uint8_t)(secsSince1900 >> 24);
    pkt[41] = (uint8_t)(secsSince1900 >> 16);
    pkt[42] = (uint8_t)(secsSince1900 >> 8);
    pkt[43] = (uint8_t)(secsSince1900);
}

// 2026-08-30 00:00:00 UTC = 1787011200 Unix = +2208988800 NTP
static const unsigned long kUnix = 1787011200UL;
static const unsigned long kNtp  = kUnix + 2208988800UL;

static FakeUdp *g_udp = nullptr;
static NtpAsync *g_ntp = nullptr;

void setUp(void)
{
    mc_test_set_millis(10000);
    g_udp = new FakeUdp();
    g_ntp = new NtpAsync(*g_udp);
    g_ntp->setPoolServerIP(kServer);
    g_ntp->begin();
}

void tearDown(void)
{
    delete g_ntp; g_ntp = nullptr;
    delete g_udp; g_udp = nullptr;
}

// ------------------------------------------------------------------- Tests

// Backlog scenario 1: gateway off, a request is pending, the reply arrives
// -> the harvest sets the time. Before this fix there was no code path at
// all that read the socket while bGATEWAY was off (getMeshComUDP()/getUDP()
// -- the only callers of tryConsume() -- both sat inside the gateway gate),
// so the reply was never read and the node always timed out.
static void test_harvest_liest_die_antwort_ohne_gateway(void)
{
    g_ntp->loop();                     // sendRequest(): request now pending
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);
    TEST_ASSERT_FALSE(g_ntp->isTimeSet());

    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, kNtp);
    g_udp->queue(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply));

    mc_test_advance_millis(40);
    ntpHarvestReply(*g_udp, *g_ntp);   // the fix: no gateway needed to read this

    TEST_ASSERT_TRUE(g_ntp->isTimeSet());
    TEST_ASSERT_EQUAL_UINT32(kUnix, (uint32_t)g_ntp->getEpochTime());
    TEST_ASSERT_EQUAL_INT(1, g_udp->parsePacketCalls);
    TEST_ASSERT_EQUAL_INT(1, g_udp->readCalls);
}

// Backlog scenario 2: gateway off, a non-NTP datagram arrives -> drained
// without effect. There is no gateway consumer on this path (no GATE/CONF/
// BEAT parsing happens here), so the harvest must silently discard it
// rather than leave it unread or crash.
static void test_harvest_verwirft_fremde_datagramme(void)
{
    g_ntp->loop();

    uint8_t gate[64];
    memset(gate, 0x41, sizeof(gate));
    memcpy(gate, "GATE", 4);
    g_udp->queue(IPAddress(89, 185, 97, 38), 1799, gate, sizeof(gate));

    ntpHarvestReply(*g_udp, *g_ntp);

    TEST_ASSERT_FALSE(g_ntp->isTimeSet());
    TEST_ASSERT_EQUAL_INT(1, g_udp->parsePacketCalls);
    TEST_ASSERT_EQUAL_INT(1, g_udp->readCalls);
    TEST_ASSERT_FALSE(g_udp->hasQueued);   // drained, not left in the socket

    // the real reply still gets through on the next pass
    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, kNtp);
    g_udp->queue(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply));
    ntpHarvestReply(*g_udp, *g_ntp);
    TEST_ASSERT_TRUE(g_ntp->isTimeSet());
}

// With nothing queued, the harvest is a single cheap parsePacket() and
// nothing else -- no read() call, no state change.
static void test_harvest_ohne_datagramm_tut_nichts(void)
{
    g_ntp->loop();
    ntpHarvestReply(*g_udp, *g_ntp);

    TEST_ASSERT_EQUAL_INT(1, g_udp->parsePacketCalls);
    TEST_ASSERT_EQUAL_INT(0, g_udp->readCalls);
    TEST_ASSERT_FALSE(g_ntp->isTimeSet());
}

// Backlog scenario 3: gateway-on path unchanged, no double-consume. The
// gateway's own receive path (getMeshComUDP()/getUDP()) always called
// tryConsume() directly on the datagram it already read off the socket --
// ntpHarvestReply() is a separate, additive entry point the gateway-on
// branch never calls (the call-site guard in esp32_main.cpp/nrf52_main.cpp
// is if/else on bGATEWAY, so the two never run in the same loop pass).
// What's left to prove at this level: calling tryConsume() the way the
// gateway path always has still behaves identically -- ntpHarvestReply()
// added a second way to reach it, not a second read of any one datagram.
static void test_gateway_pfad_bleibt_unveraendert(void)
{
    g_ntp->loop();

    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, kNtp);

    // this is exactly what getMeshComUDP()/getUDP() do: they already own
    // the datagram (parsePacket()+read() happened as part of their own
    // gateway receive loop) and hand it to tryConsume() directly --
    // ntpHarvestReply() is never in that call chain.
    TEST_ASSERT_TRUE(g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply)));
    TEST_ASSERT_TRUE(g_ntp->isTimeSet());

    // the socket was never touched by ntpHarvestReply() for this datagram
    TEST_ASSERT_EQUAL_INT(0, g_udp->parsePacketCalls);
    TEST_ASSERT_EQUAL_INT(0, g_udp->readCalls);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_harvest_liest_die_antwort_ohne_gateway);
    RUN_TEST(test_harvest_verwirft_fremde_datagramme);
    RUN_TEST(test_harvest_ohne_datagramm_tut_nichts);
    RUN_TEST(test_gateway_pfad_bleibt_unveraendert);
    return UNITY_END();
}
