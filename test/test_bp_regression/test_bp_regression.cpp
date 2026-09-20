// Integrierte Regressions-Suite fuer den DJ8MEH-Vorfall 2026-08-31
// (Feldlog console-dj8meh-8-2026-08-31.log, RCA in docs/archive/
// bp-rca-fixes-impl-plan-20260831.md): ein Node wies User-Nachrichten 8
// Minuten lang ab, obwohl die reale Sendequeue nach ~2 Minuten leer war.
//
// Die Einzel-Fixes sind je fuer sich unit-getestet (test_txring: BP-02/03,
// test_backpressure: BP-04, test_bp_notice_frame + test_extern_notice_json:
// BP-01-Rahmung). DIESE Suite prueft das ZUSAMMENSPIEL am echten Ring
// (txring_functions.cpp) plus echter Statemaschine (backpressure.h), so
// verdrahtet wie in sendMessage()/bpPollDrain(): eine Regression in
// irgendeinem der drei Fixes macht mindestens einen Fall hier rot.
//
// Kompletter BP-Regressionslauf ueber alle Suiten -- ZWEI Kommandos, je Env
// mit seinen eigenen Filtern (pio versucht sonst jede Suite in jedem
// gelisteten Env zu bauen und meldet ERRORED statt sie zu ueberspringen):
//
//   pio test -e native      -f test_backpressure -f test_extern_notice_json
//   pio test -e native_aprs -f test_txring -f test_txring_flood \
//                           -f test_bp_notice_frame -f test_bp_regression
//
//   pio test -e native_aprs -f test_bp_regression   (nur diese Suite)

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <txring_functions.h>
#include <aprs_functions.h>
#include <backpressure.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
// (identischer Stub-Satz wie test_txring.cpp; die Ring-Globals definiert
// txring_functions.cpp im NATIVE_BUILD-Zweig env-weit selbst.)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // int, nicht uint8_t: ODR, siehe test_txring.cpp
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

static void resetRing(void)
{
    strcpy(meshcom_settings.node_call, "DK5EN-90");
    memset(ringBuffer, 0, sizeof(ringBuffer));
    iWrite = 0;
    iRead = 0;
    memset(retryCount, 0, sizeof(retryCount));
    memset(ringPriority, 0, sizeof(ringPriority));
    memset(ringEnqueueTime, 0, sizeof(ringEnqueueTime));
    memset(stat_drop_count, 0, sizeof(stat_drop_count));
    stat_queue_hwm = 0;
    mc_test_set_millis(1000);
}

void setUp(void) { resetRing(); }
void tearDown(void) {}

// ---- Frame-Bau (Layout wie von addTxRingEntry/getMessagePriority gelesen,
// Kurzform des Builders aus test_txring.cpp) --------------------------------

static uint16_t buildRawFrame(uint8_t *out, uint8_t type, uint32_t id,
                              const char *srcpath, const char *dst,
                              const char *payload)
{
    uint16_t n = 0;
    out[n++] = type;
    out[n++] = (uint8_t)(id & 0xFF);
    out[n++] = (uint8_t)((id >> 8) & 0xFF);
    out[n++] = (uint8_t)((id >> 16) & 0xFF);
    out[n++] = (uint8_t)((id >> 24) & 0xFF);
    out[n++] = 0x00; // byte5 (Flags/Hop)
    if (srcpath != nullptr)
    {
        size_t sl = strlen(srcpath);
        memcpy(out + n, srcpath, sl); n += (uint16_t)sl;
        out[n++] = '>';
        size_t dl = strlen(dst);
        memcpy(out + n, dst, dl); n += (uint16_t)dl;
        out[n++] = type; // Ziel-Terminator (bei TEXT ':')
    }
    size_t pl = strlen(payload);
    memcpy(out + n, payload, pl); n += (uint16_t)pl;
    return n;
}

/// Gruppen-Text wie die DJ8MEH-Testnachrichten (dst "9" -> Prio HIGH=2,
/// user_msg-Status 0x00 = Retransmission aktiv).
static int enqueueUserText(uint32_t id)
{
    uint8_t f[300];
    uint16_t len = buildRawFrame(f, 0x3A, id, "DJ8MEH-8", "9",
                                 "Test1 ueber HF - Regression");
    return addTxRingEntry(f, len, 0x00, "user_msg");
}

/// HEY-Relay wie der Blocker 70A28124 aus dem Feldlog (Typ 0x40 -> Prio
/// BACKGROUND=5, fire-and-forget-Status 0xFF).
static int enqueueHeyRelay(uint32_t id)
{
    uint8_t f[300];
    uint16_t len = buildRawFrame(f, 0x40, id, nullptr, nullptr, "R9;9,73,5;");
    return addTxRingEntry(f, len, 0xFF, "rx_relay");
}

/// Simuliert den doTX-Konsum: Slot geleert (wie lora_functions.cpp:1569),
/// danach rueckt der Lesezeiger wie im Original vor.
static void drainAllSlotsExcept(int keep_slot)
{
    for (int i = 0; i < MAX_RING; i++)
        if (i != keep_slot)
            ringBuffer[i][0] = 0;
    advanceIReadPastEmpty();
}

// --------------------------------------------------------------------------
// T1 — Der Vorfall, Ende-zu-Ende: Burst -> korrektes QRT -> realer Drain,
// nur der ausgehungerte Prio-5-HEY bleibt am Lesezeiger -> die Episode
// MUSS binnen QRV_HOLD_MS schliessen und der Node wieder annehmen.
//
// Kontrollrechnung Altverhalten (beides im Feld belegt):
//  - BP-02 kaputt (Indexdistanz): txRingDepth() nach dem Drain == 17 (iRead
//    steht am Blocker, iWrite dahinter) -> poll() sieht nie das Wasserband,
//    refusing() bleibt true -> Assert "!refusing" rot.
//  - BP-04 kaputt (QRV nur bei 0): Tiefe bleibt 1 (der Blocker) -> QRV
//    kommt nie -> Assert "QRV nach 10 s" rot.
// Im Feld dauerte genau das 8 Minuten (Refuses 15:43:36-15:43:58, QRV erst
// 15:51:22, ausgeloest durch zufaellige Eviction des Blockers).
//
// BP-05 2026-08-31 (Kontrollrechnung fuer diesen Testaufbau, MAX_RING 20):
// der Blocker besetzt Slot 0 (Tiefe 1) VOR der Schleife; die Statemaschine
// bekommt die Tiefe erst NACH jedem User-Enqueue zu sehen. Tiefe je
// Iteration i (0-indiziert) = 1 (Blocker) + (i+1) (User) = i+2, also QRS
// (Schwelle 5) beim 4. User-Enqueue (i=3, Tiefe 5) -- nicht bei Tiefe 2 wie
// vor BP-05. Der einzige hier gepinnte Wert ist aber qrs_seen==1 (genau
// einmal pro Episode), unabhaengig von der konkreten Tiefe, an der das
// passiert -- dieser Assert war schon vor BP-05 tiefenunabhaengig und bleibt
// unveraendert gueltig.
static void test_dj8meh_episode_endet_nach_realem_drain(void)
{
    BackPressure bp(MAX_RING);

    // Blocker zuerst: HEY-Relay am kuenftigen Lesezeiger (Slot 0).
    int blocker = enqueueHeyRelay(0x70A28124u);
    TEST_ASSERT_EQUAL_INT(0, blocker);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_BACKGROUND, ringPriority[blocker]);

    // User-Burst wie DJ8MEH (Nachricht 1..N), verdrahtet wie sendMessage():
    // nach jedem Enqueue bekommt die Statemaschine die neue Tiefe.
    int qrs_seen = 0, qrt_seen = 0;
    uint32_t id = 0xE9ED0082u;
    for (int i = 0; i < MAX_RING - 2 && !bp.refusing(); i++)
    {
        int slot = enqueueUserText(id++);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, slot);
        BpNotice n = bp.onSend(txRingDepth(), false, millis());
        if (n == BP_NOTICE_QRS) qrs_seen++;
        if (n == BP_NOTICE_QRT) qrt_seen++;
    }
    TEST_ASSERT_TRUE_MESSAGE(bp.refusing(), "Burst muss QRT ausloesen");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrs_seen, "genau ein QRS pro Episode");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrt_seen, "genau ein QRT pro Episode");

    // Nachricht 8-10 des Feldlogs: Refuses, Episoden-Notice weiterhin nur
    // einmal (oben gepinnt) -- aber BP-07: onRefuse() ist jetzt BpNack, kein
    // BpNotice mehr, und liefert bei JEDER Abweisung BP_NACK_QRT (nicht
    // laenger latched). Das Feldlog selbst kannte diese Rueckmeldung noch
    // nicht -- Luecke L1, siehe docs/backpressure-flow-control.md Kapitel 8.
    TEST_ASSERT_EQUAL_INT(BP_NACK_QRT, bp.onRefuse());

    // Realer Drain: alles gesendet ausser dem prioritaets-ausgehungerten
    // Blocker -- exakt der Ringzustand 15:45-15:50 im Feldlog.
    drainAllSlotsExcept(blocker);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iRead); // Blocker pinnt den Lesezeiger
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, txRingDepth(),
        "BP-02: ehrliche Tiefe 1 (alte Indexdistanz haette 17+ gemeldet)");

    // bpPollDrain()-Verdrahtung: Wasserband armiert, Halt 10 s, dann QRV.
    uint32_t t0 = millis();
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(txRingDepth(), t0));
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(txRingDepth(), t0 + 5000));
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_QRV,
        bp.poll(txRingDepth(), t0 + BackPressure::QRV_HOLD_MS),
        "BP-04: QRV nach 10 s Wasserband (alt: nie, ausser Tiefe 0)");
    TEST_ASSERT_FALSE_MESSAGE(bp.refusing(),
        "Node nimmt nach der Episode wieder an -- im Feld dauerte das 8 min");
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE,
        bp.poll(txRingDepth(), t0 + BackPressure::QRV_HOLD_MS + 1000));
}

// --------------------------------------------------------------------------
// T2 — Der zweite Ausweg: der Blocker selbst altert aus dem Ring (BP-03),
// die Tiefe faellt auf 0 und die Episode schliesst SOFORT (ohne den
// 10-s-Halt). Kontrollrechnung Altverhalten: es gab keinerlei Alterung --
// der Feld-Blocker sass 10 Minuten und wurde nur durch eine zufaellige
// Prio-Eviction (RING_DROP_PRIO 15:50:34) verdraengt. Mit kaputtem Sweep
// bleibt der Slot belegt -> Asserts auf len==0/dist/QRV-sofort rot.
static void test_dj8meh_blocker_altert_aus_und_qrv_kommt_sofort(void)
{
    BackPressure bp(MAX_RING);

    int blocker = enqueueHeyRelay(0x70A28124u);
    TEST_ASSERT_EQUAL_INT(0, blocker);
    uint32_t enq_t = millis();

    // Episode oeffnen und drainieren wie in T1.
    uint32_t id = 0xE9ED0082u;
    for (int i = 0; i < MAX_RING - 2 && !bp.refusing(); i++)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, enqueueUserText(id++));
        bp.onSend(txRingDepth(), false, millis());
    }
    TEST_ASSERT_TRUE(bp.refusing());
    drainAllSlotsExcept(blocker);
    TEST_ASSERT_EQUAL_INT(1, txRingDepth());

    // 2-s-Tick-Sweep VOR der Altersgrenze: Blocker bleibt.
    mc_test_set_millis(enq_t + RING_BG_MAX_AGE_MS);
    txRingAgeBackground(millis());
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[blocker][0]);

    // Naechster Tick NACH der Grenze: Blocker faellt, Lesezeiger rueckt vor,
    // Ring ist wirklich leer -> poll() schliesst ohne Wasserband-Halt.
    mc_test_set_millis(enq_t + RING_BG_MAX_AGE_MS + 2000);
    txRingAgeBackground(millis());
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ringBuffer[blocker][0],
        "BP-03: Stale-HEY nach 3 min freigegeben (alt: sass 10 min)");
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_BACKGROUND]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)iWrite, (uint8_t)iRead);
    TEST_ASSERT_EQUAL_INT(0, txRingDepth());
    TEST_ASSERT_EQUAL_INT_MESSAGE(BP_NOTICE_QRV, bp.poll(txRingDepth(), millis()),
        "Tiefe 0 schliesst sofort, kein 10-s-Halt noetig");
    TEST_ASSERT_FALSE(bp.refusing());
}

// --------------------------------------------------------------------------
// T3 — Gegenprobe gegen Ueberkorrektur: ein ECHTER Burst (keine Loecher)
// muss QRT weiterhin genauso ausloesen wie im Feld (QRT an der 80-%-Schwelle,
// Refuses danach). Die ehrliche Tiefe darf den Schutz nicht abschwaechen.
//
// BP-05 2026-08-31: QRS feuert hier (ohne Blocker, Tiefe = Anzahl User-
// Enqueues) erst bei Tiefe 5, nicht mehr bei Tiefe 2 -- die alte Formulierung
// "QRS bei Tiefe 2 wie im Feldlog 15:42:15" ist damit obsolet: im Feldlog
// selbst loeste genau diese Tiefe-2-Reaktion faelschlich QRS waehrend
// normalen Betriebs aus (Grundlast 1-4, drei falsche QRS/QRV-Paare in
// 5,5 Minuten), was ueberhaupt erst zur neuen, fixen Schwelle 5 gefuehrt hat.
// Unter der neuen Schwelle bleibt ein ECHTER Burst (kein Loch, jede Tiefe
// 1..threshold durchlaufen) trotzdem frueh erkannt: QRS kommt mit der
// dritten eigenen Nachricht ab der Linie (QRS_MIN_USER_MSGS, 2026-09-01:
// Tiefe 5, 6, 7 -> QRS bei 7; die Tiefe allein zaehlt Relay/ACK mit und
// warnte einen Gateway-Absender schon bei seiner ersten Nachricht), QRT
// weiterhin exakt an der 80-%-Schwelle -- die Ueberkorrektur-Sorge dieses
// Tests bleibt entkraeftet.
static void test_qrt_ausloesung_bleibt_intakt(void)
{
    BackPressure bp(MAX_RING);

    int qrs_at_depth = -1, qrt_at_depth = -1;
    uint32_t id = 0xC0FFEE00u;
    for (int i = 0; i < MAX_RING - 1 && !bp.refusing(); i++)
    {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, enqueueUserText(id++));
        int d = txRingDepth();
        BpNotice n = bp.onSend(d, false, millis());
        if (n == BP_NOTICE_QRS) qrs_at_depth = d;
        if (n == BP_NOTICE_QRT) qrt_at_depth = d;
    }
    TEST_ASSERT_TRUE(bp.refusing());
    TEST_ASSERT_EQUAL_INT_MESSAGE(bp.qrsThreshold() + BackPressure::QRS_MIN_USER_MSGS - 1, qrs_at_depth,
        "QRS weiterhin beim Aufbau: dritte eigene Nachricht ab der BP-05-Schwelle (Tiefe 7)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(bp.refuseThreshold(), qrt_at_depth,
        "QRT weiterhin exakt an der 80-%-Schwelle, wie im Feldlog 15:43:25");
    // Ein frisch belegter Ring darf NICHT vorzeitig schliessen: Tiefe ist
    // ueber dem Wasserband, poll() entwaffnet nur.
    TEST_ASSERT_EQUAL_INT(BP_NOTICE_NONE, bp.poll(txRingDepth(), millis()));
    TEST_ASSERT_TRUE(bp.refusing());
}

// --------------------------------------------------------------------------
// T4 — BP-07 fails-before fuer Luecke L1 (bp-l1-l4-impl-plan.md, "Fails-
// before-Nachweis fuer L1"): onRefuse() aendert die Rueckgabetyp-Signatur
// (BpNotice -> BpNack), also kann kein Unit-Test auf der Statemaschine
// allein den Vorher-Zustand woertlich nachbauen. Der ehrliche Fails-before
// sitzt deshalb hier -- echter Ring, echte Statemaschine, verdrahtet wie
// sendMessage() es ab BP-07 tut: erst bp.refusing() pruefen, nur wenn frei
// tatsaechlich enqueuen, sonst onRefuse() nacken.
//
// BackPressure bp(10) statt bp(MAX_RING): die Statemaschine kennt nur ihre
// eigene Schwelle, nicht die physische Ringgroesse dieser Env (hier 20) --
// bp(10) emuliert das "MAX_RING 10, 13 lokale Sends" aus dem Plan (T-Beam-
// Groesse) unabhaengig davon, auf welchem realen Ring die Nachrichten
// tatsaechlich landen. refuseThreshold() = 8, qrsThreshold() = 5.
//
// 13 Sends ohne jeden Abfluss: Sends 1-8 werden enqueued (Tiefe erreicht 8
// == Schwelle -> QRT beim 8.), Sends 9-13 treffen auf refusing() == true --
// 5 Abweisungen, 5 Nacks (onRefuse() == BP_NACK_QRT bei jeder einzelnen),
// 1 QRS (Tiefe 5), 1 QRT (Tiefe 8).
//
// Vorher (Stand vor BP-07): onRefuse() war BpNotice und lieferte konstant
// BP_NOTICE_NONE (Befund L1, docs/backpressure-flow-control.md Kapitel 8) --
// die 5 Abweisungen waeren identisch gewesen, aber 0 davon haetten je einen
// Nack ausgeloest. Der Zaehler nacks == 0 waere hier rot gewesen.
static void test_flood_13_into_10_yields_five_nacks(void)
{
    BackPressure bp(10);

    int queued = 0, refused = 0, nacks = 0, qrs_seen = 0, qrt_seen = 0;
    uint32_t id = 0xA0000000u;

    for(int i = 0; i < 13; i++)
    {
        if(bp.refusing())
        {
            refused++;
            if(bp.onRefuse() == BP_NACK_QRT)
                nacks++;
            continue;
        }

        int slot = enqueueUserText(id++);
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0, slot);
        queued++;

        BpNotice n = bp.onSend(txRingDepth(), slot < 0, millis());
        if(n == BP_NOTICE_QRS) qrs_seen++;
        if(n == BP_NOTICE_QRT) qrt_seen++;
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(8, queued, "8 von 13 muessen den Ring erreichen");
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, refused, "die restlichen 5 muessen abgewiesen werden");
    TEST_ASSERT_EQUAL_INT_MESSAGE(5, nacks, "BP-07: jede Abweisung nackt, nicht nur die erste (fails-before: 0)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrs_seen, "QRS genau einmal pro Episode");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, qrt_seen, "QRT genau einmal pro Episode");
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, txRingDepth(), "kein Abfluss in diesem Test -- 8 bleiben im Ring stehen");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_dj8meh_episode_endet_nach_realem_drain);
    RUN_TEST(test_dj8meh_blocker_altert_aus_und_qrv_kommt_sofort);
    RUN_TEST(test_qrt_ausloesung_bleibt_intakt);
    RUN_TEST(test_flood_13_into_10_yields_five_nacks);
    return UNITY_END();
}
