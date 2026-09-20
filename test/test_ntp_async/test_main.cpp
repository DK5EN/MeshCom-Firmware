// TM-35: der NTP-Client darf die Schleife nicht blockieren und darf keine
// Gateway-Pakete vom gemeinsamen Socket wegfressen.
//
// Regression gegen den alten NTPClient:
//   - update() blockierte bis zu 1 s (delay(10) x 100) im loopTask
//   - forceUpdate() leerte vor dem Senden JEDES wartende Datagramm
//     ("while(parsePacket()) flush()") -- also auch GATE/CONF-Frames
#include <unity.h>

#include "Arduino.h"
#include "ntp_async.h"

// ------------------------------------------------------------------ Fake-UDP

struct FakeUdp : public UDP
{
    uint16_t boundPort = 0;
    int      packetsSent = 0;
    IPAddress lastDest;
    uint16_t lastDestPort = 0;
    uint8_t  lastPayload[64] = {0};
    size_t   lastLen = 0;
    bool     failSend = false;

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
};

static const IPAddress kServer(162, 159, 200, 1);

// NTP-Antwort mit transmit timestamp == secs (Sekunden seit 1900)
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

static void test_begin_bindet_den_ntp_port(void)
{
    TEST_ASSERT_EQUAL_UINT16(NTP_ASYNC_LOCAL_PORT, g_udp->boundPort);
}

static void test_loop_sendet_genau_eine_anfrage_und_wartet(void)
{
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);
    TEST_ASSERT_EQUAL_UINT16(NTP_ASYNC_SERVER_PORT, g_udp->lastDestPort);
    TEST_ASSERT_TRUE(g_udp->lastDest == kServer);
    TEST_ASSERT_EQUAL_size_t(NTP_ASYNC_PACKET_SIZE, g_udp->lastLen);
    TEST_ASSERT_EQUAL_UINT8(0xE3, g_udp->lastPayload[0]);   // LI/VN/Mode client

    // solange die Antwort aussteht, wird nicht nachgeschossen
    for(int i = 0; i < 50; i++)
    {
        mc_test_advance_millis(10);
        g_ntp->loop();
    }
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);
    TEST_ASSERT_FALSE(g_ntp->isTimeSet());
}

static void test_antwort_setzt_die_zeit(void)
{
    g_ntp->loop();

    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, kNtp);

    mc_test_advance_millis(40);
    TEST_ASSERT_TRUE(g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply)));
    TEST_ASSERT_TRUE(g_ntp->isTimeSet());
    TEST_ASSERT_EQUAL_UINT32(kUnix, (uint32_t)g_ntp->getEpochTime());
    TEST_ASSERT_EQUAL_STRING("00:00:00", g_ntp->getFormattedTime().c_str());

    // Uhr laeuft ohne weitere Anfrage weiter
    mc_test_advance_millis(3661000);
    TEST_ASSERT_EQUAL_UINT32(kUnix + 3661UL, (uint32_t)g_ntp->getEpochTime());
    TEST_ASSERT_EQUAL_STRING("01:01:01", g_ntp->getFormattedTime().c_str());
}

static void test_fremde_pakete_bleiben_fuer_das_gateway(void)
{
    g_ntp->loop();

    // GATE-Frame vom MeshCom-Server, gleiche Groesse wie eine NTP-Antwort
    uint8_t gate[NTP_ASYNC_PACKET_SIZE];
    memset(gate, 0x41, sizeof(gate));
    memcpy(gate, "GATE", 4);

    // vom Gateway-Server, Port 1799 -> nicht unseres
    TEST_ASSERT_FALSE(g_ntp->tryConsume(IPAddress(89, 185, 97, 38), 1799, gate, sizeof(gate)));
    // richtige Quelle, aber Gateway-Port -> nicht unseres
    TEST_ASSERT_FALSE(g_ntp->tryConsume(kServer, 1799, gate, sizeof(gate)));
    // NTP-Port, aber andere Quelle -> nicht unseres
    TEST_ASSERT_FALSE(g_ntp->tryConsume(IPAddress(89, 185, 97, 38), NTP_ASYNC_SERVER_PORT, gate, sizeof(gate)));
    // zu kurz -> nicht unseres
    TEST_ASSERT_FALSE(g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, gate, 20));

    TEST_ASSERT_FALSE(g_ntp->isTimeSet());
}

static void test_client_mode_paket_wird_nicht_akzeptiert(void)
{
    g_ntp->loop();

    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, kNtp, 2, 3);   // Mode 3 = client, keine Serverantwort
    TEST_ASSERT_FALSE(g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply)));
    TEST_ASSERT_FALSE(g_ntp->isTimeSet());
}

static void test_unsinnige_zeitstempel_setzen_die_uhr_nicht(void)
{
    g_ntp->loop();

    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, 100UL);        // vor 1970
    TEST_ASSERT_TRUE(g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply)));
    TEST_ASSERT_FALSE(g_ntp->isTimeSet());

    makeReply(reply, kNtp, 0);      // Stratum 0 = kiss-of-death
    TEST_ASSERT_TRUE(g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply)));
    TEST_ASSERT_FALSE(g_ntp->isTimeSet());
}

static void test_timeout_wiederholt_mit_backoff(void)
{
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);

    // Timeout laeuft ab -> keine sofortige Wiederholung, sondern nach 5 s
    mc_test_advance_millis(3000);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);

    mc_test_advance_millis(4999);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);

    mc_test_advance_millis(2);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(2, g_udp->packetsSent);

    // nach drei Fehlversuchen langsamer Takt (60 s)
    for(int i = 0; i < 2; i++)
    {
        mc_test_advance_millis(3000);
        g_ntp->loop();          // Timeout
        mc_test_advance_millis(5001);
        g_ntp->loop();          // Wiederholung
    }
    int sent = g_udp->packetsSent;

    mc_test_advance_millis(3000);
    g_ntp->loop();              // Timeout Nr. 4 -> jetzt 60 s
    mc_test_advance_millis(30000);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(sent, g_udp->packetsSent);
    mc_test_advance_millis(30001);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(sent + 1, g_udp->packetsSent);
}

static void test_nach_erfolg_erst_nach_dem_intervall_wieder(void)
{
    g_ntp->loop();

    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, kNtp);
    g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply));

    g_ntp->setUpdateInterval(900000UL);
    // das Intervall zaehlt ab der Antwort; setUpdateInterval aendert den
    // bereits gesetzten Faelligkeitszeitpunkt nicht rueckwirkend
    mc_test_advance_millis(899000);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);

    mc_test_advance_millis(2000);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(2, g_udp->packetsSent);

    // requestNow() zieht die naechste Anfrage sofort vor
    mc_test_advance_millis(3000);
    g_ntp->loop();              // Timeout der laufenden Anfrage
    g_ntp->requestNow();
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(3, g_udp->packetsSent);
}

static void test_serverwechsel_fragt_sofort_neu(void)
{
    g_ntp->loop();
    uint8_t reply[NTP_ASYNC_PACKET_SIZE];
    makeReply(reply, kNtp);
    g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply));
    TEST_ASSERT_EQUAL_INT(1, g_udp->packetsSent);

    const IPAddress hamnet(44, 143, 0, 9);
    g_ntp->setPoolServerIP(hamnet);
    g_ntp->loop();
    TEST_ASSERT_EQUAL_INT(2, g_udp->packetsSent);
    TEST_ASSERT_TRUE(g_udp->lastDest == hamnet);

    // die alte Quelle darf die Uhr jetzt nicht mehr stellen
    makeReply(reply, kNtp + 1000);
    TEST_ASSERT_FALSE(g_ntp->tryConsume(kServer, NTP_ASYNC_SERVER_PORT, reply, sizeof(reply)));
}

static void test_ohne_server_wird_nicht_gesendet(void)
{
    FakeUdp udp;
    NtpAsync ntp(udp);
    ntp.begin();
    for(int i = 0; i < 10; i++)
    {
        mc_test_advance_millis(10000);
        ntp.loop();
    }
    TEST_ASSERT_EQUAL_INT(0, udp.packetsSent);
    TEST_ASSERT_FALSE(ntp.isTimeSet());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_begin_bindet_den_ntp_port);
    RUN_TEST(test_loop_sendet_genau_eine_anfrage_und_wartet);
    RUN_TEST(test_antwort_setzt_die_zeit);
    RUN_TEST(test_fremde_pakete_bleiben_fuer_das_gateway);
    RUN_TEST(test_client_mode_paket_wird_nicht_akzeptiert);
    RUN_TEST(test_unsinnige_zeitstempel_setzen_die_uhr_nicht);
    RUN_TEST(test_timeout_wiederholt_mit_backoff);
    RUN_TEST(test_nach_erfolg_erst_nach_dem_intervall_wieder);
    RUN_TEST(test_serverwechsel_fragt_sofort_neu);
    RUN_TEST(test_ohne_server_wird_nicht_gesendet);
    return UNITY_END();
}
