// TM-31 / upstream #568: was der TX-Ring tut, wenn ein Gateway schneller
// gefuettert wird als es senden kann.
//
// Auf der Bank gemessen (gwflood_fixed_settle300_20260830.json, 30 UDP-Frames
// bei 8/4/2/1/0.5 s Abstand): nichts geht am Eingang verloren, der Ring fuellt
// sich auf queued=19/20, und danach verwirft die Firmware das ANKOMMENDE
// Paket -- 6x RING_DROP_NEW "queue full, no lower prio to evict" -- waehrend
// ein Positionsframe vorher noch das eigene HEY des Knotens verdraengt hat
// (RING_DROP_PRIO). Diese Suite haelt genau diese Politik fest:
//
//   * gleiche Prioritaet + voller Ring -> Tail-Drop (der NEUE faellt),
//   * hoehere Prioritaet + voller Ring -> Head-Drop (der schlechteste Bestand
//     wird geraeumt),
//
// und damit auch, welche Nachrichtenart eine Flut ueberlebt: Broadcast-Text
// (HIGH) und ACK (CRITICAL) verdraengen Positionen, Positionen verdraengen
// HEY, und eine Position gegen lauter Positionen verliert.
//
// test_txring deckt die beiden Randfaelle CRITICAL-gegen-LOW (Eviction) und
// LOW-gegen-CRITICAL (Drop) ab; hier stehen die Faelle, die die Bankmessung
// tatsaechlich getroffen hat, plus die Flut selbst.
//
//   pio test -e native_aprs -f test_txring_flood

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <txring_functions.h>
#include <aprs_functions.h>
#include <backpressure.h>          // BP-01: die Schwellen gegen den ECHTEN Ring
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
// (identischer Stub-Satz wie test_txring.cpp)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

static void resetRing(void)
{
    // TX-01: the ring refuses an unconfigured node (XX0XXX, the shim default),
    // so the fixture needs a real callsign before any enqueue.
    strcpy(meshcom_settings.node_call, "DK5EN-90");
    memset(ringBuffer, 0, sizeof(ringBuffer));
    iWrite = 0;
    iRead = 0;
    memset(retryCount, 0, sizeof(retryCount));
    memset(ringPriority, 0, sizeof(ringPriority));
    memset(ringEnqueueTime, 0, sizeof(ringEnqueueTime));
    memset(stat_drop_count, 0, sizeof(stat_drop_count));
    stat_queue_hwm = 0;
    mc_test_set_millis(0);
}

void setUp(void) { resetRing(); }
void tearDown(void) {}

// ---- Frame-Bau (Layout wie in test_txring.cpp) ------------------------------

struct BuiltFrame
{
    uint8_t bytes[300];
    uint16_t len;
};

static BuiltFrame buildFrame(uint8_t type, uint32_t id, uint8_t byte5,
                             const char *srcpath, const char *dst,
                             const uint8_t *payload, uint16_t payload_len)
{
    BuiltFrame f{};
    uint16_t n = 0;
    f.bytes[n++] = type;
    f.bytes[n++] = (uint8_t)(id & 0xFF);
    f.bytes[n++] = (uint8_t)((id >> 8) & 0xFF);
    f.bytes[n++] = (uint8_t)((id >> 16) & 0xFF);
    f.bytes[n++] = (uint8_t)((id >> 24) & 0xFF);
    f.bytes[n++] = byte5;

    if (srcpath != nullptr)
    {
        size_t sl = strlen(srcpath);
        memcpy(f.bytes + n, srcpath, sl); n += (uint16_t)sl;
        f.bytes[n++] = '>';
        size_t dl = strlen(dst);
        memcpy(f.bytes + n, dst, dl); n += (uint16_t)dl;
        f.bytes[n++] = type; // Ziel-Terminator (bei TEXT == ':')
    }

    if (payload_len > 0)
        memcpy(f.bytes + n, payload, payload_len);
    n += payload_len;
    f.len = n;
    return f;
}

static BuiltFrame buildPositionFrame(uint32_t id)
{
    static const uint8_t payload[] = "4825.35N\\01147.19E-Test";
    return buildFrame(MSG_TYPE_POSITION, id, 0x12, nullptr, nullptr, payload, sizeof(payload) - 1);
}

static BuiltFrame buildHeyFrame(uint32_t id)
{
    static const uint8_t payload[] = "R0;";
    return buildFrame(MSG_TYPE_HEY, id, 0x92, nullptr, nullptr, payload, sizeof(payload) - 1);
}

static BuiltFrame buildBroadcastTextFrame(uint32_t id, const char *payload)
{
    // Ziel "*" -> getMessagePriority() == MSG_PRIO_HIGH (Broadcast), also genau
    // die Klasse, um die es in #568 geht (BBS-Listing an alle).
    return buildFrame(MSG_TYPE_TEXT, id, 0x14, "OE1XAR-62,DK5EN-91", "*",
                      (const uint8_t *)payload, (uint16_t)strlen(payload));
}

static BuiltFrame buildAckFrame(uint32_t id)
{
    static const uint8_t payload[4] = {0xEF, 0xBE, 0xAD, 0xDE};
    return buildFrame(MSG_TYPE_ACK, id, 0x84, nullptr, nullptr, payload, sizeof(payload));
}

// Fuellt den Ring bis an die Ueberlaufkante (MAX_RING-1 Eintraege).
static void fillWithPositions(uint32_t base)
{
    for (int i = 0; i < MAX_RING - 1; i++)
    {
        BuiltFrame f = buildPositionFrame(base + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "fill"));
    }
}

// ---- 1: gleiche Prioritaet, voller Ring -> der NEUE faellt (Tail-Drop) ------
//
// Das ist der Fall der Bankmessung: lauter UDP-Positionen im Ring, die naechste
// UDP-Position kommt an, nichts hat niedrigere Prioritaet -> RING_DROP_NEW.

static void test_gleiche_prio_voller_ring_verwirft_den_neuen(void)
{
    fillWithPositions(0x3000);

    uint8_t iReadBefore = (uint8_t)iRead;
    uint8_t iWriteBefore = (uint8_t)iWrite;

    BuiltFrame neu = buildPositionFrame(0xDEAD0001UL);
    int slot = addTxRingEntry(neu.bytes, neu.len, RING_STATUS_READY, "udp_rx");

    TEST_ASSERT_EQUAL_INT(-1, slot);                        // abgewiesen
    TEST_ASSERT_EQUAL_UINT8(iWriteBefore, (uint8_t)iWrite); // Bestand unangetastet
    TEST_ASSERT_EQUAL_UINT8(iReadBefore, (uint8_t)iRead);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[MAX_RING - 1][0]);

    // Der aelteste Eintrag lebt weiter -- Tail-Drop, nicht Head-Drop.
    TEST_ASSERT_EQUAL_UINT8(MSG_TYPE_POSITION, ringBuffer[0][2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, ringBuffer[0][3]);        // msg_id 0x3000 LE
    TEST_ASSERT_EQUAL_UINT8(0x30, ringBuffer[0][4]);
}

// ---- 2: Broadcast-Text schlaegt Position (Head-Drop) ------------------------
//
// Die #568-relevante Vorhersage: ein Textpaket (HIGH) an einen mit Positionen
// (LOW) vollen Ring wird NICHT verworfen, sondern raeumt die aelteste Position.
// Textnachrichten sind gegen eine Positionsflut also geschuetzt -- der Grund,
// warum die Bankmessung mit dem Positionskorpus f001 die haerteste Variante
// erwischt hat.

static void test_broadcast_text_verdraengt_aelteste_position(void)
{
    fillWithPositions(0x5000);

    BuiltFrame txt = buildBroadcastTextFrame(0xBEEF0001UL, "BBS listing 1/7");
    int slot = addTxRingEntry(txt.bytes, txt.len, RING_STATUS_READY, "udp_rx");

    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, slot);                     // aufgenommen
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_HIGH, ringPriority[slot]);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]);                  // Slot 0 geraeumt
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)iRead);                    // Lesezeiger vor
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_HIGH]);
}

// ---- 3: Position verdraengt HEY --------------------------------------------
//
// Auf der Bank live gesehen: RING_DROP_PRIO ... prio=5 type=40 ... replaced_by
// _prio=4 -- die UDP-Flut kostet den Knoten sein eigenes HEY.

static void test_position_verdraengt_eigenes_hey(void)
{
    for (int i = 0; i < MAX_RING - 1; i++)
    {
        BuiltFrame f = buildHeyFrame(0x7000 + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "hey"));
    }

    BuiltFrame pos = buildPositionFrame(0xC0DE0001UL);
    int slot = addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "udp_rx");

    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, slot);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_LOW, ringPriority[slot]);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]);                  // HEY geraeumt
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_BACKGROUND]);
}

// ---- 4: ACK kommt auch durch eine volle Positionsschlange ------------------

static void test_ack_kommt_auch_im_flood_durch(void)
{
    fillWithPositions(0x8000);

    BuiltFrame ack = buildAckFrame(0xAABBCCDDUL);
    int slot = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "ack");

    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, slot);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
}

// ---- 5: die Flut selbst ----------------------------------------------------
//
// 30 gleichrangige Frames ohne jeden Abfluss -- die Bankzahl. Genau MAX_RING-1
// werden angenommen, der Rest faellt; und weil der Ring per Tail-Drop verwirft,
// ueberleben die ZUERST eingetroffenen. Fuer #568 heisst das: unter Last geht
// nicht der Bestand verloren, sondern alles, was danach kommt -- bis der Sender
// die Schlange abgearbeitet hat.

static void test_flut_ohne_abfluss_nimmt_max_ring_minus_eins_an(void)
{
    const int kInjected = 30;
    int accepted = 0;
    int rejected = 0;
    int first_rejected = -1;

    for (int i = 0; i < kInjected; i++)
    {
        BuiltFrame f = buildPositionFrame(0x9000 + (uint32_t)i);
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "udp_rx");
        if (slot >= 0)
        {
            accepted++;
        }
        else
        {
            rejected++;
            if (first_rejected < 0)
                first_rejected = i;
        }
    }

    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, accepted);
    TEST_ASSERT_EQUAL_INT(kInjected - (MAX_RING - 1), rejected);
    // Der erste Verworfene ist genau der, der auf den vollen Ring trifft.
    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, first_rejected);
    TEST_ASSERT_EQUAL_UINT16((uint16_t)rejected, stat_drop_count[MSG_PRIO_LOW]);
    // Hochwassermarke: MAX_RING, nicht MAX_RING-1. addTxRingEntry() zieht
    // stat_queue_hwm hoch, BEVOR die Ueberlaufentscheidung faellt -- die Marke
    // zaehlt also auch den Eintrag mit, der gleich darauf verworfen wird. Ein
    // gemeldeter HWM von MAX_RING heisst im Feld deshalb "Ring war voll UND es
    // wurde verworfen", nicht "Ring war randvoll und es ging gerade noch".
    TEST_ASSERT_EQUAL_UINT16((uint16_t)MAX_RING, stat_queue_hwm);

    // Der aelteste Frame (0x9000) steht unveraendert in Slot 0.
    TEST_ASSERT_EQUAL_UINT8(MSG_TYPE_POSITION, ringBuffer[0][2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, ringBuffer[0][3]);
    TEST_ASSERT_EQUAL_UINT8(0x90, ringBuffer[0][4]);
}

// ---- 6: ein Textstrom ueberlebt die Flut, ein Positionsstrom nicht ---------
//
// Direkt die #568-Frage: dasselbe Fuellmuster, einmal mit Positionen und einmal
// mit Broadcast-Text als Nachzuegler. Der Text kommt durch (auf Kosten des
// Bestands), die Position nicht.

static void test_text_kommt_durch_wo_position_faellt(void)
{
    fillWithPositions(0xA000);
    BuiltFrame pos = buildPositionFrame(0xA0FF0001UL);
    TEST_ASSERT_EQUAL_INT(-1, addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "udp_rx"));

    resetRing();

    fillWithPositions(0xA000);
    BuiltFrame txt = buildBroadcastTextFrame(0xA0FF0002UL, "BBS listing 2/7");
    TEST_ASSERT_TRUE(addTxRingEntry(txt.bytes, txt.len, RING_STATUS_READY, "udp_rx") >= 0);
}

// ---- 6b: der Gateway-Relay-Pfad klassifiziert Text als NORMAL, nicht HIGH --
//
// getMeshComUDPpacket() reiht mit Status 0xFF ein (== RING_STATUS_DONE, "keine
// Wiederholung"), und getMessagePriority() liest genau dieses Byte als
// "weitergeleitet" -> MSG_PRIO_NORMAL statt der pfadbasierten Broadcast-Stufe
// MSG_PRIO_HIGH. Auf der Bank sichtbar als "replaced_by_prio=3" in jeder
// RING_DROP_PRIO-Zeile des gemischten Laufs (gwflood_mixed_20260830.json).
//
// Fuer #568 aendert das nichts an der Rangfolge -- NORMAL (3) schlaegt
// Position (4) weiterhin -- aber wer die Bankausgabe liest, darf sich an der 3
// nicht stossen, und wer den Statuswert des Relay-Pfads aendert, verschiebt
// damit ungewollt die Prioritaet des gesamten UDP-Verkehrs.

static void test_relay_text_ist_normal_und_schlaegt_position_trotzdem(void)
{
    fillWithPositions(0xB000);

    BuiltFrame txt = buildBroadcastTextFrame(0xB0FF0001UL, "BBS listing 3/7");
    int slot = addTxRingEntry(txt.bytes, txt.len, RING_STATUS_DONE, "udp_rx");

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_NORMAL, ringPriority[slot]);   // nicht HIGH
    TEST_ASSERT_TRUE(MSG_PRIO_NORMAL < MSG_PRIO_LOW);               // schlaegt Position
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]);                   // Position geraeumt
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
}

// ---- 7: die Ringgroesse ist eine bewusste Entscheidung ---------------------
//
// MAX_RING bestimmt, wie lange ein Gateway eine Flut puffern kann, bevor es
// verwirft: auf der Bank ~1 Frame / 20 s Abfluss, also puffert der Ring rund
// 6 Minuten Sendearbeit. Wer den Wert aendert, aendert damit die Antwort auf
// #568 -- dieser Test macht die Aenderung sichtbar statt still.

static void test_ringgroesse_ist_dokumentiert(void)
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(BOARD_RAK4630) || defined(NATIVE_BUILD)
    TEST_ASSERT_EQUAL_INT(20, MAX_RING);
#endif
    TEST_ASSERT_TRUE(MAX_RING >= 10);
    TEST_ASSERT_TRUE(MAX_RING <= 255);   // iWrite/iRead sind 8 Bit
}

// ---- 8: BP-01 -- txRingDepth() ist der Ring, nicht eine zweite Buchfuehrung -
//
// Der Rueckmeldepfad an den Absender (QRS/QRT/QTA/QRV) haengt komplett an
// txRingDepth(). Wenn diese Zahl vom tatsaechlichen Fuellstand abweicht, warnt
// der Knoten zum falschen Zeitpunkt -- oder gar nicht. Also gegen den echten
// Ring gemessen, nicht gegen eine Nachbildung.

static void test_txringdepth_folgt_dem_echten_ring(void)
{
    TEST_ASSERT_EQUAL_INT(0, txRingDepth());

    for (int i = 0; i < MAX_RING - 1; i++)
    {
        BuiltFrame f = buildPositionFrame(0xC000 + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "fill"));
        TEST_ASSERT_EQUAL_INT(i + 1, txRingDepth());
    }

    // Abfluss: doTX() leert einen Slot und schiebt den Lesezeiger nach.
    ringBuffer[0][0] = 0;
    advanceIReadPastEmpty();
    TEST_ASSERT_EQUAL_INT(MAX_RING - 2, txRingDepth());
}

// Die 80-%-Schwelle, an der der Knoten aufhoert, lokale Nutzernachrichten
// anzunehmen -- gefahren vom echten Ring, nicht von einer Zahlenreihe. Auf
// dieser Env ist MAX_RING 20, die Schwelle also 16; die Arithmetik selbst
// (10/20/30) steht in test_backpressure.
static void test_backpressure_qrt_an_der_echten_80_prozent_marke(void)
{
    BackPressure bp(MAX_RING);
    const int threshold = bp.refuseThreshold();

    TEST_ASSERT_EQUAL_INT((MAX_RING * 4) / 5, threshold);
    TEST_ASSERT_TRUE_MESSAGE(threshold < MAX_RING - 1, "Schwelle muss unter der Ueberlaufkante liegen");

    int qrs_count = 0;
    int qrt_count = 0;
    bool refused_before_threshold = false;

    for (int i = 0; i < threshold; i++)
    {
        BuiltFrame f = buildPositionFrame(0xD000 + (uint32_t)i);
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "user_msg");
        TEST_ASSERT_TRUE(slot >= 0);

        if (bp.refusing())
            refused_before_threshold = true;

        switch (bp.onSend(txRingDepth(), slot < 0))
        {
            case BP_NOTICE_QRS: qrs_count++; break;
            case BP_NOTICE_QRT: qrt_count++; break;
            default: break;
        }
    }

    TEST_ASSERT_FALSE_MESSAGE(refused_before_threshold, "unterhalb 80 % darf nichts abgewiesen werden");
    TEST_ASSERT_EQUAL_INT(threshold, txRingDepth());
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrs_count, "genau ein QRS pro Episode");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrt_count, "genau ein QRT, beim Erreichen der Schwelle");
    TEST_ASSERT_TRUE_MESSAGE(bp.refusing(), "ab 80 % werden lokale Nutzernachrichten abgewiesen");
}

// Und die Gegenprobe am oberen Ende: der Ring verwirft (RING_DROP_NEW, Fall 1
// oben), txRingDepth() bleibt stehen, und daraus wird ein QTA -- die einzige
// Stelle, an der der Absender erfaehrt, dass seine Nachricht weg ist.
static void test_backpressure_qta_wenn_der_echte_ring_verwirft(void)
{
    BackPressure bp(MAX_RING);

    fillWithPositions(0xE000);
    bp.onSend(txRingDepth(), false);
    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, txRingDepth());

    BuiltFrame neu = buildPositionFrame(0xE0FF0001UL);
    int slot = addTxRingEntry(neu.bytes, neu.len, RING_STATUS_READY, "user_msg");

    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_INT_MESSAGE(MAX_RING - 1, txRingDepth(), "ein verworfener Frame veraendert die Tiefe nicht");
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QTA, bp.onSend(txRingDepth(), slot < 0));

    // Abfluss bis in das Ruheband -> genau ein QRV schliesst die Episode.
    memset(ringBuffer, 0, sizeof(ringBuffer));
    iRead = 0;
    iWrite = 0;
    TEST_ASSERT_EQUAL_INT(0, txRingDepth());
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_QRV, bp.poll(txRingDepth()));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(txRingDepth()));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_gleiche_prio_voller_ring_verwirft_den_neuen);
    RUN_TEST(test_broadcast_text_verdraengt_aelteste_position);
    RUN_TEST(test_position_verdraengt_eigenes_hey);
    RUN_TEST(test_ack_kommt_auch_im_flood_durch);
    RUN_TEST(test_flut_ohne_abfluss_nimmt_max_ring_minus_eins_an);
    RUN_TEST(test_text_kommt_durch_wo_position_faellt);
    RUN_TEST(test_relay_text_ist_normal_und_schlaegt_position_trotzdem);
    RUN_TEST(test_ringgroesse_ist_dokumentiert);
    RUN_TEST(test_txringdepth_folgt_dem_echten_ring);
    RUN_TEST(test_backpressure_qrt_an_der_echten_80_prozent_marke);
    RUN_TEST(test_backpressure_qta_wenn_der_echte_ring_verwirft);
    return UNITY_END();
}
