// Native Testsuite fuer den TX-Ring-Kern (getMessagePriority, getNextTxSlot,
// addTxRingEntry) -- nach txring_functions.cpp
// extrahiert (QA-Welle 2026-08-22), weil genau dieser Code dem Projekteigner
// ausdruecklich misstraut wird: N-14 (zwei Schreiber teilten sich denselben
// Slot), die Prioritaets-Klassifizierung und die Overflow/Eviction-Logik.
//
// Goldene Referenz ist der wortwoertlich verschobene Code, Stand Commit
// efb2381b (vor der Extraktion) -- diese Suite fixiert das TATSAECHLICHE
// Verhalten dieses Codes, nicht eine Wunschvorstellung davon. Wo der Code
// selbst eine Ecke hat (siehe TXRING-CLEARSLOT-GAP unten), wird das
// Verhalten bewusst mit-getestet statt stillschweigend uebergangen.
//
//   pio test -e native_aprs -f test_txring

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <txring_functions.h>
#include <txring_ttl.h>
#include <aprs_functions.h>
#include <dm_stats.h>             // M0-1: ringstat_enqueue/ringstat_parked_overwrite
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
// (identischer Stub-Satz wie test_aprs_spec.cpp/test_hey_report.cpp -- jede
// native_aprs-Testsuite linkt aprs_functions.cpp mit ein, siehe
// build_src_filter in platformio.ini)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
// int, nicht uint8_t: loop_functions_extern.h (unten eingebunden) deklariert
// `extern int BOARD_HARDWARE;` -- test_aprs_spec.cpp/test_hey_report.cpp
// definieren stattdessen `uint8_t BOARD_HARDWARE`, was ohne den direkten
// Include von loop_functions_extern.h in derselben TU nicht auffaellt (siehe
// Deviations-Vermerk im Wave-Report).
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

// ---- Globals, die txring_functions.cpp per extern erwartet -----------------
// Definiert sind sie NICHT hier, sondern in txring_functions.cpp selbst
// (NATIVE_BUILD-Zweig): loop_functions.cpp (die kanonische Definitionsstelle,
// siehe loop_functions_extern.h) wird im native_aprs-Env nicht mitgebaut, und
// build_src_filter verlinkt txring_functions.cpp env-weit in JEDES
// Test-Executable (auch test_aprs_decode/test_aprs_corpus/test_hey_report,
// die nichts davon brauchen) -- Definitionen hier wuerden dort mit
// txring_functions.cpp kollidieren (doppelte Symbole). loop_functions_extern.h
// (bereits oben eingebunden) liefert die extern-Deklarationen.

static void resetRing(void)
{
    // TX-01: the ring refuses an unconfigured node (XX0XXX, the shim default),
    // so the fixture needs a real callsign before any enqueue.
    strcpy(meshcom_settings.node_call, "DK5EN-90");
    memset(ringBuffer, 0, sizeof(ringBuffer));
    memset(retryCount, 0, sizeof(retryCount));
    memset(ringPriority, 0, sizeof(ringPriority));
    memset(ringEnqueueTime, 0, sizeof(ringEnqueueTime));
    memset(ringSource, 0, sizeof(ringSource));
    memset(ringNeed, 0, sizeof(ringNeed));
    memset(ringAlone, 0, sizeof(ringAlone));
    memset(ringKind, 0, sizeof(ringKind));
    memset(stat_drop_count, 0, sizeof(stat_drop_count));
    stat_queue_hwm = 0;
    stat_supersede_count = 0;
    mc_test_set_millis(0);
    // Nachbarschaftsmatrix Stufe 2 (Feldlauf 23.09.): jeder Test startet mit
    // --nbrrelay aus, sonst wuerde ein liegen gebliebenes bNBRCANCEL=true aus
    // einem frueheren Test den naechsten unbemerkt mitfaerben.
    bNBRCANCEL = false;
    bNBRRELAY = false;
    // M0-1 (0.4): reset between tests, same as every other stat_* counter above.
    ringstat_enqueue.store(0);
    ringstat_parked_overwrite.store(0);
}

static const CetClock kNoClock = {false, false, 0};
void setUp(void) { resetRing(); }
void tearDown(void) {}

// --------------------------------------------------------------- Frame-Bau
//
// Baut den Frame-Teil, der addTxRingEntry als `frame` uebergeben wird (OHNE
// die Ring-Header-Bytes [0]=len/[1]=status -- die setzt addTxRingEntry
// selbst). Layout exakt wie von getMessagePriority()/addTxRingEntry()
// gelesen: [0]=type, [1..4]=msg_id LE, [5]=byte5(Flags/Hop), danach je nach
// Typ optional "SRCPATH>DEST" + Typ-Byte als Ziel-Terminator (das Typ-Byte
// ist bei TEXT 0x3A == ':' -- deshalb prueft getMessagePriority() auf
// ':' ODER MSG_TYPE_TEXT), dann die Nutzlast.
//
// srcpath == nullptr baut den pfadlosen Rahmen (ACK/POSITION/HEY -- deren
// Prioritaet haengt nicht vom Pfad ab, siehe getMessagePriority()).
//
// payload_ring_offset ist der Slot-Offset (ringBuffer[slot]+X), an dem die
// Nutzlast beginnt -- wird programmatisch waehrend des Baus mitgefuehrt statt
// von Hand ausgerechnet, damit sich kein Off-by-one einschleicht.

struct BuiltFrame
{
    uint8_t bytes[300];
    uint16_t len;
    uint16_t payload_ring_offset;
    uint16_t payload_len;
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

    f.payload_ring_offset = (uint16_t)(2 + n); // +2: ring[0]=len, ring[1]=status
    if (payload_len > 0)
        memcpy(f.bytes + n, payload, payload_len);
    n = (uint16_t)(n + payload_len);
    f.payload_len = payload_len;
    f.bytes[n++] = 0x00; // Terminator (fuer TEXT relevant, sonst harmlos)
    f.len = n;
    return f;
}

static BuiltFrame buildTextFrame(const char *dst, const char *payload,
                                  uint32_t id = 0x01020304UL,
                                  const char *srcpath = "DK5EN-90")
{
    return buildFrame(MSG_TYPE_TEXT, id, 0x14, srcpath, dst,
                       (const uint8_t *)payload, (uint16_t)strlen(payload));
}

static BuiltFrame buildAckFrame(uint32_t id = 0xAABBCCDDUL)
{
    static const uint8_t payload[4] = {0xEF, 0xBE, 0xAD, 0xDE};
    return buildFrame(MSG_TYPE_ACK, id, 0x84, nullptr, nullptr, payload, sizeof(payload));
}

static BuiltFrame buildPositionFrame(uint32_t id = 0x11223344UL)
{
    static const uint8_t payload[] = "4825.35N\\01147.19E-Test";
    return buildFrame(MSG_TYPE_POSITION, id, 0x12, nullptr, nullptr, payload, sizeof(payload) - 1);
}

static BuiltFrame buildHeyFrame(uint32_t id = 0x55667788UL)
{
    static const uint8_t payload[] = "R0;";
    return buildFrame(MSG_TYPE_HEY, id, 0x92, nullptr, nullptr, payload, sizeof(payload) - 1);
}

// Fuellt Slot `slot` von Hand (fuer direkte getMessagePriority()-Aufrufe,
// ohne den Umweg ueber addTxRingEntry).
static void fillSlot(int slot, const BuiltFrame &f, uint8_t status)
{
    ringBuffer[slot][0] = (uint8_t)f.len;
    ringBuffer[slot][1] = status;
    memcpy(&ringBuffer[slot][2], f.bytes, f.len);
}

// ----------------------------------------------------- Test 1: Einfacher Enqueue

static void test_einfacher_enqueue(void)
{
    mc_test_set_millis(12345);
    BuiltFrame f = buildPositionFrame();

    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t1");

    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_READY, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(f.bytes, &ringBuffer[slot][2], f.len);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_LOW, ringPriority[slot]);
    TEST_ASSERT_EQUAL_UINT32(12345, ringEnqueueTime[slot]);
}

// ----------------------------------------------------- Test 2: Prioritaets-Klassifizierung

static void expectPriorityDirect(const BuiltFrame &f, uint8_t status, uint8_t expected, const char *label)
{
    resetRing();
    fillSlot(0, f, status);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, getMessagePriority(0), label);
}

static void expectPriorityViaEnqueue(const BuiltFrame &f, uint8_t status, uint8_t expected, const char *label)
{
    resetRing();
    int slot = addTxRingEntry(f.bytes, f.len, status, "t2");
    TEST_ASSERT_TRUE_MESSAGE(slot >= 0, label);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, ringPriority[slot], label);
}

static void expectPriority(const BuiltFrame &f, uint8_t status, uint8_t expected, const char *label)
{
    expectPriorityDirect(f, status, expected, label);
    expectPriorityViaEnqueue(f, status, expected, label);
}

static void test_prioritaets_klassifizierung(void)
{
    // ACK -> CRITICAL, unabhaengig vom Pfad
    expectPriority(buildAckFrame(), RING_STATUS_READY, MSG_PRIO_CRITICAL, "ACK");

    // Text-DM (persoenliches Rufzeichen als Ziel) -> CRITICAL
    expectPriority(buildTextFrame("DK5EN-91", "Hallo"), RING_STATUS_READY, MSG_PRIO_CRITICAL, "Text-DM");

    // Text an '*' (Broadcast) -> HIGH
    expectPriority(buildTextFrame("*", "Hallo Welt"), RING_STATUS_READY, MSG_PRIO_HIGH, "Text-Broadcast");

    // Text an Gruppe '9999' -> HIGH
    expectPriority(buildTextFrame("9999", "Gruppen-Text"), RING_STATUS_READY, MSG_PRIO_HIGH, "Text-Gruppe");

    // Text mit Status RING_STATUS_DONE (Relay) -> NORMAL, Ziel wird ignoriert
    expectPriority(buildTextFrame("DK5EN-91", "Relay"), RING_STATUS_DONE, MSG_PRIO_NORMAL, "Text-Relay");

    // Position -> LOW
    expectPriority(buildPositionFrame(), RING_STATUS_READY, MSG_PRIO_LOW, "Position");

    // HEY -> BACKGROUND
    expectPriority(buildHeyFrame(), RING_STATUS_READY, MSG_PRIO_BACKGROUND, "HEY");

    // unbekannter Typ -> NORMAL (Default-Zweig)
    BuiltFrame unknown = buildFrame(0x99, 0x01UL, 0x00, nullptr, nullptr, nullptr, 0);
    expectPriority(unknown, RING_STATUS_READY, MSG_PRIO_NORMAL, "Typ 0x99");
}

// -------------------------------------------- Test 2b: addTxRingEntryOnce()
//
// P15: SendAckMessage()/sendPing()/SendPong() und die Einmal-Texte von
// sendMessage() ({ping}, {CET}/{SET}/{MCP}, RM1 -- dmTextSendOnce()) wollten
// alle dasselbe -- Status DONE speichern (keine
// Wiederholung), aber als eigene DM/Gruppen-/Broadcast-Nachricht
// klassifizieren, nicht als Relay (die TEXT-Falle: ein VORAB auf DONE
// gesetzter Status stuft getMessagePriority() als "Relay" ein, siehe Test
// oben "Text-Relay"). SendAckMessage() reihte deshalb bisher mit temp-READY
// ein und schrieb den Status NACH addTxRingEntry() per Hand nach --
// ausserhalb des Locks, auf nRF52 ein Fenster fuer eine Race mit doTX() im
// anderen Task. Diese Tests pinnen addTxRingEntryOnce()s Ersatz dafuer:
// Klassifizierung UND Status-Schreiben in einem Aufruf.
//
// bNBRCANCEL bleibt hier false (resetRing()) und kind/need/alone bleiben auf
// ihren Defaults (RING_KIND_OTHER, 0, 0) -- keiner dieser Slots kann also in
// die Nachbarschaftsmatrix-Stufe-2-Fall-B-Sperre (txringInCaseBHold())
// geraten, die nur RING_KIND_RELAY-Slots mit ringAlone==0 haelt.

static void test_add_tx_ring_entry_once_dm_wird_critical_und_done(void)
{
    BuiltFrame f = buildTextFrame("DK5EN-91", "Hallo");

    int slot = addTxRingEntryOnce(f.bytes, f.len, "t2b");

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
}

static void test_add_tx_ring_entry_once_gruppe_wird_high_und_done(void)
{
    BuiltFrame f = buildTextFrame("9999", "Gruppen-Text");

    int slot = addTxRingEntryOnce(f.bytes, f.len, "t2b");

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_HIGH, ringPriority[slot]);
}

static void test_add_tx_ring_entry_once_broadcast_wird_high_und_done(void)
{
    BuiltFrame f = buildTextFrame("*", "Hallo Welt");

    int slot = addTxRingEntryOnce(f.bytes, f.len, "t2b");

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_HIGH, ringPriority[slot]);
}

// Falle pinnen (und Alt-Verhalten von addTxRingEntry() unveraendert lassen):
// wer eine eigene DM per addTxRingEntry() weiterhin VORAB auf DONE setzt,
// bekommt weiter NORMAL statt CRITICAL -- genau das ist der Fehler, den
// addTxRingEntryOnce() fuer seine Aufrufer beseitigt, aber addTxRingEntry()
// selbst darf sich fuer seine (unveraenderten) Aufrufer nicht anders
// verhalten als bisher.
static void test_add_tx_ring_entry_mit_done_klassifiziert_weiter_als_relay(void)
{
    BuiltFrame f = buildTextFrame("DK5EN-91", "Hallo");

    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "t2b_plain");

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_NORMAL, ringPriority[slot]);
}

// RCA 2026-10-10: ein RM1-Frame (DM) geht genau einmal raus und muss dabei
// als eigene DM (CRITICAL) klassifiziert werden, nicht als Relay. Der Weg
// dafuer ist addTxRingEntryOnce(), wie ihn sendMessage() ueber
// dmTextSendOnce() jetzt fuer alle Einmal-Texte nimmt.
static void test_rm1_dm_once_wird_critical_und_done(void)
{
    BuiltFrame f = buildTextFrame("DK5EN-92", "RM1 17 ok p=2/22 0123456789abcdef");

    int slot = addTxRingEntryOnce(f.bytes, f.len, "user_msg");

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
}

// Wave 3a: an RM1 COMMAND is enqueued like a PN (status 0x00): own-DM priority
// CRITICAL, not DONE, so the retransmit ladder picks it up.
static void test_rm1_command_status0_wird_critical_und_nicht_done(void)
{
    BuiltFrame f = buildTextFrame("DK5EN-92", "RM1 17 status 0123456789abcdef");

    int slot = addTxRingEntry(f.bytes, f.len, 0x00, "user_msg", 0);

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_TRUE(ringBuffer[slot][1] != RING_STATUS_DONE);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
}

// Falle pinnen (Vorher-Verhalten von sendMessage()): derselbe RM1-Frame mit
// VORAB gesetztem Status DONE gilt als Relay (NORMAL, 4500 ms Backoff) und
// kollidiert mit den Relays der Nachbarn. Spiegel von "t2b_plain" oben.
static void test_rm1_dm_mit_status_done_vorab_ist_relay_klasse(void)
{
    BuiltFrame f = buildTextFrame("DK5EN-92", "RM1 17 ok p=2/22 0123456789abcdef");

    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "user_msg", 0);

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_NORMAL, ringPriority[slot]);
}

// getNextTxSlot() waehlt READY oder DONE aus, uebergeht aber jeden Status
// dazwischen (SENT..Schwelle, "wartet auf Retransmit-Timer"). Ein per
// addTxRingEntryOnce() eingereihter Slot landet mit DONE im Ring: er wird
// also EINMAL zur Sendung ausgewaehlt (wie READY), aber nie durch die
// Retransmit-Statusfolge erneut vorgemerkt -- das ist der gesamte
// Mechanismus, der eine Wiederholung verhindert (doTX()/
// updateRetransmissionStatus() selbst sitzen in lora_functions.cpp, nativ
// hier nicht mitgebaut, siehe env:native_aprs build_src_filter). RING_KIND_OTHER
// (Default) heisst auch: kein Fall-B-Hold, der Slot waere sonst nicht sofort
// waehlbar.
static void test_add_tx_ring_entry_once_slot_ist_fuer_getnexttxslot_done(void)
{
    BuiltFrame f = buildTextFrame("DK5EN-91", "Hallo");

    int slot = addTxRingEntryOnce(f.bytes, f.len, "t2b");

    TEST_ASSERT_TRUE(slot >= 0);
    TEST_ASSERT_EQUAL_INT(slot, getNextTxSlot());
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
}

// Neue beobachtbare Konsequenz des Fixes: ein Ring voller Relay-Eintraege
// (NORMAL, Prio 3) verdraengt frueher hoechstens LOW/BACKGROUND (Prio 4/5) --
// eine eigene DM, die (vor P15) wegen des vorab gesetzten DONE-Status
// ebenfalls als NORMAL eingestuft wurde, war gleich/nicht niedriger prio als
// jeder Ring-Eintrag und wurde beim Overflow VERWORFEN (droppedNew-Zweig,
// "same or lower priority than everything in queue"). Ueber
// addTxRingEntryOnce() klassifiziert dieselbe DM CRITICAL (Prio 1) und
// verdraengt jetzt den aeltesten Relay-Eintrag, statt selbst zu
// verschwinden. Die Relay-Fuellung geht ueber addTxRingEntry(..., DONE, ...)
// mit Default-kind (RING_KIND_OTHER) -- kein RING_KIND_RELAY, also auch kein
// Fall-B-Hold, der die Eviction-Auswahl hier verzerren wuerde.
static void test_add_tx_ring_entry_once_verdraengt_relay_statt_verworfen_zu_werden(void)
{
    // Pool mit MAX_RING Relay-Eintraegen (Text, Status DONE -> NORMAL,
    // siehe getMessagePriority()) voll fuellen.
    for (int i = 0; i < MAX_RING; i++)
    {
        BuiltFrame f = buildTextFrame("DK5EN-91", "relay", (uint32_t)(0x4000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "t2b_relayfill");
        TEST_ASSERT_EQUAL_INT(i, slot);
        TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_NORMAL, ringPriority[slot]);
    }

    BuiltFrame dm = buildTextFrame("DK5EN-92", "dringend", 0x5000UL);
    int slot = addTxRingEntryOnce(dm.bytes, dm.len, "t2b_evict");

    // Slot 0 (aeltester Relay-Eintrag) wurde verdraengt, die DM uebernimmt
    // genau diesen Slot (Pool: kein Umzug, kein freier Slot sonst).
    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dm.bytes, &ringBuffer[slot][2], dm.len);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_NORMAL]);
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());
}

// ----------------------------------------------------- Test 3: Ring-Wrap

static void test_ring_wrap(void)
{
    // TXP W1b (Slot-Pool, vorher Indexrand-Wrap): Phase 1 fuellt den Pool
    // KOMPLETT -- alle MAX_RING Slots (20, nicht mehr 19) nehmen Eintraege auf.
    for (int i = 0; i < MAX_RING; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x1000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t3");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());

    // Alle Slots wieder freigeben (simuliert vollstaendige Konsumierung durch doTX).
    for (int i = 0; i < MAX_RING; i++)
        ringBuffer[i][0] = 0;
    TEST_ASSERT_EQUAL_INT(0, txRingDepth());

    // Phase 2: ein freigegebener Slot ist sofort wieder belegbar, jeweils der
    // ERSTE freie (Slot 0, 1, 2) -- es gibt keinen Schreibzeiger mehr.
    int expected[3] = {0, 1, 2};
    for (int i = 0; i < 3; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x2000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t3wrap");
        TEST_ASSERT_EQUAL_INT_MESSAGE(expected[i], slot, "erster freier Slot");
    }
}

// ----------------------------------------------------- Test 4: Overflow mit Eviction

static void test_overflow_mit_eviction(void)
{
    // Pool mit MAX_RING LOW-Prio-Eintraegen (Position) voll fuellen. Die
    // Enqueue-Zeit steigt mit dem Index, Slot 0 ist der aelteste.
    for (int i = 0; i < MAX_RING; i++)
    {
        mc_test_set_millis((uint32_t)(100 + i));
        BuiltFrame f = buildPositionFrame((uint32_t)(0x3000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t4fill");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }

    // Eine CRITICAL-DM enqueuen -> muss den aeltesten (= Slot 0, kleinste
    // ringEnqueueTime) LOW-Eintrag verdraengen und dessen Slot uebernehmen.
    mc_test_set_millis(1000);
    BuiltFrame dm = buildTextFrame("DK5EN-91", "dringend");
    int slot = addTxRingEntry(dm.bytes, dm.len, RING_STATUS_READY, "t4evict");

    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dm.bytes, &ringBuffer[slot][2], dm.len);
    TEST_ASSERT_EQUAL_UINT32(1000, ringEnqueueTime[slot]);
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());

    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
}

// ----------------------------------------------- Eviction im Pool (ersetzt N-24)
//
// TXP W1b: die N-24-Umzugslogik (TXRING-ORPHAN-SLOT: iRead-Eintrag in den
// geraeumten Slot umziehen) entfaellt mit dem Lesezeiger. Der Pool verdraengt
// den AELTESTEN Eintrag (ringEnqueueTime, wrap-sicher) der SCHLECHTESTEN
// Prioritaet; der neue Eintrag uebernimmt genau diesen Slot, kein anderer
// Eintrag wandert, nichts wird still unsichtbar.
static void test_eviction_trifft_aeltesten_der_schlechtesten_prio_und_nichts_wandert(void)
{
    // Slot 0: CRITICAL (beste Prio, bleibt unangetastet)
    BuiltFrame ack = buildAckFrame(0xC0FFEEUL);
    int slot0 = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "evict0");
    TEST_ASSERT_EQUAL_INT(0, slot0);

    // Slots 1..4: LOW (Position) -- Fuellmaterial
    for (int i = 1; i <= 4; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x5000 + i));
        addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "evictfill");
    }

    // Slot 5: BACKGROUND (HEY), Zeit 500 -- schlechteste Prio.
    mc_test_set_millis(500);
    BuiltFrame hey = buildHeyFrame(0x6000UL);
    TEST_ASSERT_EQUAL_INT(5, addTxRingEntry(hey.bytes, hey.len, RING_STATUS_READY, "evictworst"));

    // Slots 6..MAX_RING-1: LOW-Fuellmaterial; ein zweites BACKGROUND (HEY) auf
    // dem HOEHEREN Index 9, aber AELTER (Zeit 100 < 500).
    for (int i = 6; i < MAX_RING; i++)
    {
        if (i == 9)
        {
            mc_test_set_millis(100);
            BuiltFrame h2 = buildHeyFrame(0x6100UL);
            TEST_ASSERT_EQUAL_INT(9, addTxRingEntry(h2.bytes, h2.len, RING_STATUS_READY, "evictworst2"));
            mc_test_set_millis(0);
            continue;
        }
        BuiltFrame f = buildPositionFrame((uint32_t)(0x7000 + i));
        addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "evictfill2");
    }
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());

    // Ein LOW-Eintrag (4) ist besser als BACKGROUND (5): verdraengt wird das
    // AELTERE der beiden HEYs (Slot 9, Zeit 100), nicht Slot 5 (Zeit 500)
    // und nicht Slot 0.
    mc_test_set_millis(2000);
    BuiltFrame trigger = buildPositionFrame(0x8000UL);
    int slotNew = addTxRingEntry(trigger.bytes, trigger.len, RING_STATUS_READY, "evicttrigger");
    TEST_ASSERT_EQUAL_INT(9, slotNew);

    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_BACKGROUND]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(trigger.bytes, &ringBuffer[9][2], trigger.len);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_LOW, ringPriority[9]);

    // Nichts ist umgezogen: Slot 0 haelt weiter den ACK, Slot 5 das andere HEY.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ack.len, ringBuffer[0][0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ack.bytes, &ringBuffer[0][2], ack.len);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)hey.len, ringBuffer[5][0]);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_BACKGROUND, ringPriority[5]);

    // getNextTxSlot() sieht den CRITICAL-Eintrag weiter (Slot 0).
    TEST_ASSERT_EQUAL_INT(0, getNextTxSlot());
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_CRITICAL]);
}

// -------------------------------------------- SL-03/SL-06: ringSource[]
//
// addTxRingEntry() leitet die Herkunftskennung aus dem `source`-Label ab
// (setlogRingSourceCode() in setlog_lines.h). Bei der Prio-Verdraengung
// uebernimmt der neue Eintrag den Slot und MUSS seine eigene Herkunft setzen,
// nicht die des verdraengten Eintrags behalten.
static void test_sl06_ringsource_aus_label_und_bei_verdraengung_kopiert(void)
{
    // Slot 0: Relay-Weiterleitung eines Empfangs -> 'r', Prio CRITICAL
    // (bleibt bei der Eviction-Auswahl unangetastet).
    BuiltFrame ack = buildAckFrame(0xC0FFEEUL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "rx_ack_fwd"));
    TEST_ASSERT_EQUAL_INT('r', (int)ringSource[0]);

    // Slots 1..4: eigene Positionen -> 'o'
    for (int i = 1; i <= 4; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x5000 + i));
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "user_pos"));
        TEST_ASSERT_EQUAL_INT('o', (int)ringSource[i]);
    }

    // Slot 5: HEY -> schlechteste Prio im Fenster, wird verdraengt.
    BuiltFrame hey = buildHeyFrame(0x6000UL);
    TEST_ASSERT_EQUAL_INT(5, addTxRingEntry(hey.bytes, hey.len, RING_STATUS_READY, "beacon"));
    TEST_ASSERT_EQUAL_INT('o', (int)ringSource[5]);

    for (int i = 6; i < MAX_RING; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x7000 + i));
        addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "user_pos");
    }

    // Vom Server eingespeist -> 'g'; verdraengt Slot 5 (HEY) und uebernimmt ihn.
    BuiltFrame trigger = buildPositionFrame(0x8000UL);
    int slotNew = addTxRingEntry(trigger.bytes, trigger.len, RING_STATUS_READY, "udp_rx");
    TEST_ASSERT_EQUAL_INT(5, slotNew);

    // aus dem Label des neuen Eintrags, nicht das verdraengte 'o' des HEY
    TEST_ASSERT_EQUAL_INT('g', (int)ringSource[slotNew]);
    // der ACK in Slot 0 ist nicht umgezogen und weiterhin 'r'.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ack.len, ringBuffer[0][0]);
    TEST_ASSERT_EQUAL_INT('r', (int)ringSource[0]);
    // unbeteiligte Slots unveraendert
    TEST_ASSERT_EQUAL_INT('o', (int)ringSource[1]);
}

// --------------------------- Nachbarschaftsmatrix Stufe 2: ringKind/ringNeed/ringAlone
//
// docs/nbr-wichtigkeit-konzept.md 5.1: drei neue Seitenfelder neben
// ringSource[], gesetzt in addTxRingEntry(). Drei Eigenschaften werden hier
// fixiert: (a) jeder Enqueue OHNE die neuen Argumente nullt sie, auch wenn
// der Slot vorher eine fremde Maske trug (kein Leck zwischen Wiederverwendungen),
// (b) mit Argumenten landen sie unveraendert im Slot, (c) die Eviction im
// Pool ueberschreibt sie im uebernommenen Slot und laesst die der anderen
// Slots stehen (der N-24-Umzug entfiel mit dem Slot-Pool).
//
// Welle 2 (edge pool): ringNeed[]/ringAlone[] sind NbrMask (nbr_mask.h), und
// addTxRingEntry() nimmt need/alone als const NbrMask* (nullptr = leere
// Maske) statt als uint32_t. maskFromU32() baut zum Vergleich mit den alten
// Bitmustern eine NbrMask, deren unterstes Wort exakt diese 32 Bit traegt --
// bequem fuer die alten festen Testwerte, ohne dass die Tests wissen muessen,
// wie viele Woerter NBR_MASK_WORDS auf diesem Env hat.
static NbrMask maskFromU32(uint32_t bits)
{
    NbrMask m = nbrMaskNone();
    m.w[0] = bits;
    return m;
}

// (a) Default-Ueberschreiben: Slot 3 traegt eine simulierte Alt-Maske aus
// einem fruehereren Relay (RING_KIND_COUNTED gesetzt); ein Enqueue OHNE
// kind/need/alone in genau diesen Slot muss sie auf leer/RING_KIND_OTHER
// zuruecksetzen.
static void test_stufe2_default_ueberschreibt_alte_maske(void)
{
    // Slots 0..2 belegen, damit Slot 3 der erste freie ist.
    for (int i = 0; i < 3; i++)
    {
        BuiltFrame pre = buildPositionFrame((uint32_t)(0x9100 + i));
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(pre.bytes, pre.len, RING_STATUS_READY, "user_pos"));
    }
    ringKind[3]  = RING_KIND_RELAY | RING_KIND_COUNTED;
    ringNeed[3]  = maskFromU32(0xAAAAAAAAUL);
    ringAlone[3] = maskFromU32(0x55555555UL);

    BuiltFrame f = buildPositionFrame(0x9001UL);
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "user_pos"); // keine Stufe-2-Argumente
    TEST_ASSERT_EQUAL_INT(3, slot);

    TEST_ASSERT_EQUAL_UINT8(RING_KIND_OTHER, ringKind[slot]);
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringNeed[slot]));
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringAlone[slot]));
}

// (b) Mit Argumenten landen kind/need/alone unveraendert im Slot.
static void test_stufe2_kind_need_alone_werden_gesetzt(void)
{
    NbrMask needMask  = maskFromU32(0x0000000FUL);
    NbrMask aloneMask = maskFromU32(0x00000003UL);

    BuiltFrame f = buildPositionFrame(0x9010UL);
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "rx_relay", 0, true,
                               RING_KIND_RELAY, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_INT(0, slot);

    TEST_ASSERT_EQUAL_UINT8(RING_KIND_RELAY, ringKind[slot]);
    TEST_ASSERT_TRUE(nbrMaskEqual(needMask, ringNeed[slot]));
    TEST_ASSERT_TRUE(nbrMaskEqual(aloneMask, ringAlone[slot]));
}

// GWRX-01 (docs/rca-gwrx01-20261023.md, Option b): ein vom Server eingespeister
// TEXT-Frame ("udp_rx", ringSource 'g') wird wie rx_relay als RING_KIND_RELAY
// mit need/alone eingereiht, damit der Mithoer-Scan in OnRxDone ihn findet.

static void test_gwrx01_server_text_mit_bekanntem_bedarf_wird_relay(void)
{
    NbrMask needMask  = maskFromU32(0x0000000CUL);
    NbrMask aloneMask = maskFromU32(0x00000004UL);
    bNBRRELAY = true;

    BuiltFrame f = buildTextFrame("*", "hallo", 0x7001UL);
    int slot = addUdpRxRingEntry(f.bytes, f.len, ':', true, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_INT(0, slot);

    TEST_ASSERT_EQUAL_UINT8('g', ringSource[slot]);
    TEST_ASSERT_EQUAL_UINT8(RING_KIND_RELAY, ringKind[slot]);
    TEST_ASSERT_TRUE(nbrMaskEqual(needMask, ringNeed[slot]));
    TEST_ASSERT_TRUE(nbrMaskEqual(aloneMask, ringAlone[slot]));
    // Status bleibt DONE (0xFF): keine Wiederholung wie bisher.
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[slot][1]);
}

static void test_gwrx01_server_nichttext_bleibt_other(void)
{
    NbrMask needMask  = maskFromU32(0x0000000CUL);
    NbrMask aloneMask = maskFromU32(0x00000004UL);
    bNBRRELAY = true;

    BuiltFrame pos = buildPositionFrame(0x7002UL);
    int slot = addUdpRxRingEntry(pos.bytes, pos.len, '!', true, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT8('g', ringSource[slot]);
    TEST_ASSERT_EQUAL_UINT8(RING_KIND_OTHER, ringKind[slot]);
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringNeed[slot]));
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringAlone[slot]));

    BuiltFrame hey = buildHeyFrame(0x7003UL);
    int slot2 = addUdpRxRingEntry(hey.bytes, hey.len, '@', true, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_INT(1, slot2);
    TEST_ASSERT_EQUAL_UINT8(RING_KIND_OTHER, ringKind[slot2]);
}

static void test_gwrx01_ohne_nbrrelay_oder_unbekannt_wie_heute(void)
{
    NbrMask needMask  = maskFromU32(0x0000000CUL);
    NbrMask aloneMask = maskFromU32(0x00000004UL);

    // --nbrrelay aus
    bNBRRELAY = false;
    BuiltFrame a = buildTextFrame("*", "aus", 0x7004UL);
    int s0 = addUdpRxRingEntry(a.bytes, a.len, ':', true, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_UINT8(RING_KIND_OTHER, ringKind[s0]);
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringNeed[s0]));
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringAlone[s0]));
    TEST_ASSERT_EQUAL_UINT8('g', ringSource[s0]);

    // --nbrrelay an, aber known == false ("kein Wissen")
    bNBRRELAY = true;
    BuiltFrame b = buildTextFrame("*", "unbekannt", 0x7005UL);
    int s1 = addUdpRxRingEntry(b.bytes, b.len, ':', false, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_UINT8(RING_KIND_OTHER, ringKind[s1]);
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringNeed[s1]));
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringAlone[s1]));
}

// (c) Eviction im Pool: Slot 0 traegt eine Stufe-2-Maske und bleibt
// unangetastet; der verdraengte Slot 5 bekam sie nie und der neue Eintrag
// in Slot 5 setzt die Defaults. Eine Maske im verdraengten Slot darf nicht
// auf den Nachfolger durchschlagen.
static void test_stufe2_eviction_ueberschreibt_maske_im_uebernommenen_slot(void)
{
    NbrMask needMask  = maskFromU32(0x000000AAUL);
    NbrMask aloneMask = maskFromU32(0x00000002UL);

    BuiltFrame ack = buildAckFrame(0xC0FFEEUL);
    int slot0 = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_DONE, "rx_relay", 0, true,
                                RING_KIND_RELAY, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_INT(0, slot0);

    for (int i = 1; i <= 4; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x5100 + i));
        addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "orphanfill");
    }

    // Slot 5: HEY mit (fremder) Relay-Maske -- schlechteste Prio, wird verdraengt.
    BuiltFrame hey = buildHeyFrame(0x6100UL);
    int slot5 = addTxRingEntry(hey.bytes, hey.len, RING_STATUS_READY, "orphanworst", 0, false,
                                RING_KIND_RELAY, &needMask, &aloneMask);
    TEST_ASSERT_EQUAL_INT(5, slot5);

    for (int i = 6; i < MAX_RING; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x7100 + i));
        addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "orphanfill2");
    }

    BuiltFrame trigger = buildPositionFrame(0x8100UL);
    int slotNew = addTxRingEntry(trigger.bytes, trigger.len, RING_STATUS_READY, "orphantrigger");
    TEST_ASSERT_EQUAL_INT(5, slotNew);

    // Slot 5: Defaults des neuen Eintrags, nichts vom verdraengten HEY.
    TEST_ASSERT_EQUAL_UINT8(RING_KIND_OTHER, ringKind[5]);
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringNeed[5]));
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringAlone[5]));

    // Slot 0: nicht umgezogen, Maske unveraendert.
    TEST_ASSERT_EQUAL_UINT8(RING_KIND_RELAY, ringKind[0]);
    TEST_ASSERT_TRUE(nbrMaskEqual(needMask, ringNeed[0]));
    TEST_ASSERT_TRUE(nbrMaskEqual(aloneMask, ringAlone[0]));
}

// (d) Ein Zeilenindex >= 32 liegt jenseits des ersten Maskenworts (NBR_MASK_WORDS
// >= 2 bei den 128 Zeilen, die dieses Env ueber CONFIG_IDF_TARGET_ESP32S3
// -- siehe test/support/configuration.h -- bekommt): pinnt, dass ein Bit im
// ZWEITEN Wort addTxRingEntry()/ringNeed[]/txringInCaseBHold() genauso
// unveraendert durchlaeuft wie eines im ersten.
static void test_stufe2_maskenbit_ueber_32_ueberlebt_case_b(void)
{
    TEST_ASSERT_TRUE_MESSAGE(NBR_MAX_ROWS > 40,
        "Env liefert weniger als 41 Zeilen -- Bit 40 waere gar nicht darstellbar");

    bNBRCANCEL = true;
    mc_test_set_millis(1000);

    NbrMask need40    = nbrMaskBit(40);
    NbrMask aloneEmpty = nbrMaskNone();   // Fall B: alone leer

    BuiltFrame pos = buildPositionFrame(0xB040UL); // LOW
    int slot = addTxRingEntry(pos.bytes, pos.len, RING_STATUS_DONE, "rx_relay",
                               0, true, RING_KIND_RELAY, &need40, &aloneEmpty);
    TEST_ASSERT_EQUAL_INT(0, slot);

    TEST_ASSERT_TRUE(nbrMaskTest(ringNeed[slot], 40));
    TEST_ASSERT_FALSE(nbrMaskTest(ringNeed[slot], 39));
    TEST_ASSERT_TRUE(nbrMaskEmpty(ringAlone[slot]));

    // Fall B (ringAlone leer) -> im Hold-Fenster gehalten, wie jeder andere
    // Fall-B-Relay-Slot (txringInCaseBHold(), siehe txring_functions.h) --
    // das Bit jenseits von Wort 0 darf die Fall-A/B-Unterscheidung nicht
    // verfaelschen.
    TEST_ASSERT_TRUE(txringInCaseBHold(slot, (uint32_t)millis()));
}

// ----------------------------------------------------- Test 5: Overflow-Drop

static void test_overflow_drop(void)
{
    // Pool mit MAX_RING CRITICAL-Eintraegen (ACK) voll fuellen.
    for (int i = 0; i < MAX_RING; i++)
    {
        BuiltFrame f = buildAckFrame((uint32_t)(0x4000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t5fill");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }

    // LOW-Prio-Position enqueuen: nichts im Ring hat niedrigere Prio als CRITICAL
    // -> der NEUE Eintrag wird verworfen, nicht der Bestand.
    BuiltFrame pos = buildPositionFrame(0x9999UL);
    int slot = addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "t5drop");

    TEST_ASSERT_EQUAL_INT(-2, slot);   // RING_ENQUEUE_FULL (QRT Finding 5)
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
    // Bestand unangetastet: kein Slot frei geworden, keiner umgeschrieben.
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());
    for (int i = 0; i < MAX_RING; i++)
        TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[i]);
}

// ------------------------------------------- QRT Finding 5: eigener Code -2
// Ring voll mit Prio-1 (ACK), eigener Gruppentext (Prio 2) kann nichts
// verdraengen: Rueckgabe -2 (RING_ENQUEUE_FULL), nicht -1. Die beiden
// Validitaetsfehler (len == 0, nicht konfiguriertes Rufzeichen) bleiben -1.
static void test_qrt_f5_voll_ohne_eviction_liefert_minus2(void)
{
    for (int i = 0; i < MAX_RING; i++)
    {
        BuiltFrame f = buildAckFrame((uint32_t)(0x6000 + i));
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "f5fill"));
    }
    BuiltFrame t = buildTextFrame("9", "own text", 0x7777UL);
    int slot = addTxRingEntry(t.bytes, t.len, 0x00, "user_msg", 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-2, slot, "ring full, nothing evictable -> RING_ENQUEUE_FULL");
    int slot2 = addTxRingEntryOnce(t.bytes, t.len, "user_msg", 0);
    TEST_ASSERT_EQUAL_INT_MESSAGE(-2, slot2, "Once variant maps the same way");
}

static void test_qrt_f5_laenge_null_bleibt_minus1(void)
{
    BuiltFrame f = buildAckFrame();
    TEST_ASSERT_EQUAL_INT(-1, addTxRingEntry(f.bytes, 0, RING_STATUS_READY, "f5len"));
}

// ----------------------------------------------------- Test 5b: Laengen-Klammer
//
// Verdict Finding 3: addTxRingEntry() wies len==0/len>UDP_TX_BUF_SIZE bisher
// nirgends zurueck -- die Grenze lebte nur in den Aufrufern. Diese Tests
// fixieren die neue defensive Klammer an genau den beiden ungueltigen Raendern
// und am gueltigen Maximum (UDP_TX_BUF_SIZE == 255 Byte muss weiterhin passen,
// [2..2+255) liegt innerhalb des 260-Byte-Slots).

static void test_len_null_wird_abgewiesen(void)
{
    BuiltFrame f = buildAckFrame();
    int slot = addTxRingEntry(f.bytes, 0, RING_STATUS_READY, "t5b-null");

    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]); // Ring unangetastet
}

static void test_len_ueber_max_wird_abgewiesen(void)
{
    static uint8_t big[UDP_TX_BUF_SIZE + 1];
    memset(big, 0xAA, sizeof(big));

    int slot = addTxRingEntry(big, UDP_TX_BUF_SIZE + 1, RING_STATUS_READY, "t5b-over");

    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]); // Ring unangetastet
}

static void test_len_exakt_max_wird_enqueued(void)
{
    static uint8_t exact[UDP_TX_BUF_SIZE];
    memset(exact, 0x5A, sizeof(exact));

    int slot = addTxRingEntry(exact, UDP_TX_BUF_SIZE, RING_STATUS_READY, "t5b-exact");

    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)UDP_TX_BUF_SIZE, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exact, &ringBuffer[slot][2], UDP_TX_BUF_SIZE);
}

// ----------------------------------------------------- Test 6: getNextTxSlot

static void test_get_next_tx_slot(void)
{
    // Leerer Ring -> -1
    TEST_ASSERT_EQUAL_INT(-1, getNextTxSlot());

    // Gemischte Prios: CRITICAL gewinnt trotz spaeterer Einfuege-Reihenfolge
    resetRing();
    BuiltFrame pos = buildPositionFrame();
    BuiltFrame ack = buildAckFrame();
    BuiltFrame hey = buildHeyFrame();
    int sPos = addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "t6");
    int sAck = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "t6");
    int sHey = addTxRingEntry(hey.bytes, hey.len, RING_STATUS_READY, "t6");
    TEST_ASSERT_EQUAL_INT(sAck, getNextTxSlot());

    // sAck "konsumieren" -> naechstbester ist LOW vor BACKGROUND
    ringBuffer[sAck][0] = 0;
    TEST_ASSERT_EQUAL_INT(sPos, getNextTxSlot());

    // sPos auch konsumieren -> uebrig bleibt nur sHey
    ringBuffer[sPos][0] = 0;
    TEST_ASSERT_EQUAL_INT(sHey, getNextTxSlot());

    // sHey konsumieren -> wieder leer (alle Slots [0]==0 -> -1)
    ringBuffer[sHey][0] = 0;
    TEST_ASSERT_EQUAL_INT(-1, getNextTxSlot());

    // Gleiche Prio: FIFO -- der frueher eingereihte (kleinere ringEnqueueTime) gewinnt
    resetRing();
    BuiltFrame pos1 = buildPositionFrame(0xAAAAUL);
    BuiltFrame pos2 = buildPositionFrame(0xBBBBUL);
    int s1 = addTxRingEntry(pos1.bytes, pos1.len, RING_STATUS_READY, "t6fifo");
    int s2 = addTxRingEntry(pos2.bytes, pos2.len, RING_STATUS_READY, "t6fifo");
    TEST_ASSERT_EQUAL_INT(s1, getNextTxSlot());
    ringBuffer[s1][0] = 0; // s1 konsumiert
    TEST_ASSERT_EQUAL_INT(s2, getNextTxSlot());

    // Status SENT (nicht READY/DONE) wird uebersprungen, obwohl [0]>0
    resetRing();
    BuiltFrame ack2 = buildAckFrame();
    addTxRingEntry(ack2.bytes, ack2.len, RING_STATUS_SENT, "t6sent");
    TEST_ASSERT_EQUAL_INT(-1, getNextTxSlot());
}

// ----------------------------------------------------- Test 7: retryCountIn

static void test_retry_count_in(void)
{
    // -1 (Default) laesst retryCount[Slot] unangetastet
    retryCount[0] = 7;
    BuiltFrame a = buildAckFrame();
    int slotA = addTxRingEntry(a.bytes, a.len, RING_STATUS_READY, "t7default");
    TEST_ASSERT_EQUAL_INT(0, slotA);
    TEST_ASSERT_EQUAL_UINT8(7, retryCount[slotA]);

    // >=0 setzt retryCount[Slot] explizit
    BuiltFrame b = buildAckFrame(0x1234UL);
    int slotB = addTxRingEntry(b.bytes, b.len, RING_STATUS_READY, "t7set", 3);
    TEST_ASSERT_EQUAL_INT(1, slotB);
    TEST_ASSERT_EQUAL_UINT8(3, retryCount[slotB]);
}

// ----------------------------------------------------- Test 8: clearSlotFirst
//
// TXRING-CLEARSLOT-GAP (Fund waehrend des Schreibens dieser Suite, siehe
// Wave-Report): addTxRingEntry() nullt bei clearSlotFirst=true den Slot per
// memset(ringBuffer[w], 0x00, UDP_TX_BUF_SIZE+1) -- das sind nur die ersten
// UDP_TX_BUF_SIZE+1 (256) der UDP_TX_BUF_SIZE+5 (260) Byte breiten Slot-Zeile.
// Die letzten 4 Byte (Index 256..259) werden von KEINEM der vier Pfade
// (memset hier, memcpy der Nutzlast) je beruehrt und bleiben bei
// clearSlotFirst=true unveraendert stehen. Dieser Test fixiert genau diese
// Grenze als aktuelles Verhalten, statt sie stillschweigend zu ignorieren --
// siehe Assertion am Ende.

static void test_clear_slot_first(void)
{
    memset(ringBuffer[0], 0xFF, sizeof(ringBuffer[0])); // Alt-Muell simulieren
    ringBuffer[0][0] = 0;                               // ... in einem freien Slot (len 0)

    static const uint8_t payload[5] = {1, 2, 3, 4, 5};
    BuiltFrame f = buildFrame(MSG_TYPE_ACK, 0x0AUL, 0x00, nullptr, nullptr, payload, sizeof(payload));

    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t8", -1, true);
    TEST_ASSERT_EQUAL_INT(0, slot);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_READY, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(f.bytes, &ringBuffer[slot][2], f.len);

    // GEFIXT (TXRING-CLEARSLOT-GAP): clearSlotFirst putzt jetzt den GESAMTEN
    // Slot (sizeof statt UDP_TX_BUF_SIZE+1) -- alle Bytes vom Nutzlast-Ende
    // bis zum Slot-Ende sind genullt, auch die frueher vergessenen letzten 4.
    for (int i = 2 + f.len; i < (int)sizeof(ringBuffer[0]); i++)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ringBuffer[slot][i], "sollte genullt sein");
}

// ----------------------------------------------------- Test 9: N-14-Regression + Stress

static void test_n14_zwei_aufrufe_verschiedene_slots(void)
{
    static uint8_t patternA[24];
    static uint8_t patternB[24];
    memset(patternA, 0xAA, sizeof(patternA));
    memset(patternB, 0x55, sizeof(patternB));

    BuiltFrame a = buildFrame(MSG_TYPE_ACK, 0xA1UL, 0x00, nullptr, nullptr, patternA, sizeof(patternA));
    BuiltFrame b = buildFrame(MSG_TYPE_ACK, 0xB2UL, 0x00, nullptr, nullptr, patternB, sizeof(patternB));

    // Zwei unmittelbar aufeinanderfolgende Aufrufe, wie Loop-Task und
    // Timer-Task es nacheinander taeten (N-14: vorher konnte hier derselbe
    // Slot doppelt beschrieben werden).
    int slotA = addTxRingEntry(a.bytes, a.len, RING_STATUS_READY, "t9a");
    int slotB = addTxRingEntry(b.bytes, b.len, RING_STATUS_READY, "t9b");

    TEST_ASSERT_NOT_EQUAL(slotA, slotB);
    TEST_ASSERT_EQUAL_INT(0, slotA);
    TEST_ASSERT_EQUAL_INT(1, slotB);

    for (uint16_t i = 0; i < sizeof(patternA); i++)
        TEST_ASSERT_EQUAL_UINT8(0xAA, ringBuffer[slotA][a.payload_ring_offset + i]);
    for (uint16_t i = 0; i < sizeof(patternB); i++)
        TEST_ASSERT_EQUAL_UINT8(0x55, ringBuffer[slotB][b.payload_ring_offset + i]);
}

static void test_n14_stress_randomisiert(void)
{
    // Deterministischer LCG (kein rand()/random() -- reproduzierbar unabhaengig
    // vom Testlauf-Kontext).
    uint32_t lcg = 0x2A2A2A2AUL;
    auto nextRand = [&lcg]() -> uint32_t {
        lcg = lcg * 1103515245UL + 12345UL;
        return (lcg >> 8) & 0x7FFFFFFFUL;
    };

    // Schatten-Buchhaltung MARKER-basiert statt Slot-basiert (historisch: der
    // N-24-Fix zog Eintraege zwischen Slots um; im Pool wandert nichts mehr,
    // der Marker-Vergleich bleibt als robuste Pruefung). Der
    // Marker wird zusaetzlich als msg_id kodiert (Ring-Offset [3] = LSB der
    // LE-msg_id) -- damit ist er fuer JEDEN belegten Slot an fester Stelle
    // ablesbar, egal wohin der Eintrag gewandert ist.
    static uint8_t markerKnown[256];
    static uint16_t markerOff[256];
    static uint16_t markerPayloadLen[256];
    memset(markerKnown, 0, sizeof(markerKnown));

    static const char *dests[] = {"*", "9999", "DK5EN-91"};

    for (int iter = 0; iter < 200; iter++)
    {
        uint8_t marker = (uint8_t)((iter % 250) + 1);
        uint16_t payloadLen = (uint16_t)(1 + (nextRand() % 40));
        static uint8_t payload[64];
        for (uint16_t i = 0; i < payloadLen; i++)
            payload[i] = marker;

        int typeSel = (int)(nextRand() % 5);
        uint8_t ringStatus = RING_STATUS_READY;
        BuiltFrame f;
        uint32_t mid = (uint32_t)marker; // Marker in der msg_id, s. o.
        switch (typeSel)
        {
            case 0: f = buildFrame(MSG_TYPE_ACK, mid, 0x00, nullptr, nullptr, payload, payloadLen); break;
            case 1: f = buildFrame(MSG_TYPE_POSITION, mid, 0x00, nullptr, nullptr, payload, payloadLen); break;
            case 2: f = buildFrame(MSG_TYPE_HEY, mid, 0x00, nullptr, nullptr, payload, payloadLen); break;
            case 3: f = buildFrame(MSG_TYPE_TEXT, mid, 0x14, "DK5EN-90",
                                    dests[nextRand() % 3], payload, payloadLen); break;
            default:
                f = buildFrame(MSG_TYPE_TEXT, mid, 0x14, "DK5EN-90", "DK5EN-91", payload, payloadLen);
                ringStatus = RING_STATUS_DONE; // Relay-Pfad
                break;
        }

        int slot = addTxRingEntry(f.bytes, f.len, ringStatus, "t9stress");

        if (slot >= 0)
        {
            markerKnown[marker] = 1;
            markerOff[marker] = f.payload_ring_offset;
            markerPayloadLen[marker] = f.payload_len;
        }

        // Invariante: ein angenommener Eintrag steht im gemeldeten Slot, die
        // Belegung uebersteigt den Pool nie.
        if (slot >= 0)
            TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[slot][0]);
        TEST_ASSERT_TRUE(txRingDepth() <= MAX_RING);

        for (int s = 0; s < MAX_RING; s++)
        {
            if (ringBuffer[s][0] == 0)
                continue; // leerer Slot -- nichts zu pruefen

            char ctx[64];
            snprintf(ctx, sizeof(ctx), "iter=%d slot=%d", iter, s);

            // Belegter Slot muss aus einem getrackten Enqueue stammen: der
            // Marker steht als msg_id-LSB an fester Position [3].
            uint8_t m = ringBuffer[s][3];
            TEST_ASSERT_TRUE_MESSAGE(markerKnown[m] != 0, ctx);

            // Kein "torn write": die gesamte Nutzlast-Region traegt exakt EIN
            // Muster -- auch nach einer Eviction im Nachbarslot.
            for (uint16_t i = 0; i < markerPayloadLen[m]; i++)
            {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(m, ringBuffer[s][markerOff[m] + i], ctx);
            }
        }
    }
}

// --------------------------------------------------------- Test 10/11: BP-02
//
// txRingDepth() zaehlte bisher die reine Indexdistanz iWrite-iRead, nicht
// die tatsaechlich belegten Slots. Freigegebene Slots (Retransmit-Erfolg
// oder Eviction) HINTER einem noch belegten Slot am Lesezeiger wurden von
// der alten Arithmetik faelschlich als "noch in der Schlange" mitgezaehlt.
//
// DJ8MEH-RCA 2026-08-31: genau dieses Muster lief 8 Minuten mit
// RING_STATUS queued=19 bei real 3-4 belegten Slots, weil QRV erst bei
// Tiefe 0 schliesst.
//
// Kontrollrechnung (alte Arithmetik, Indexdistanz `(w>=r)?(w-r):(...)`):
// 5 Enqueues fuellen Slots 0..4, iWrite=5, iRead=0 -> alte Tiefe = 5, IMMER,
// unabhaengig davon, wie viele der 5 Slots danach wieder freigegeben werden.
// Gegen die alte Implementierung (verifiziert: txRingDepth() vor dem Fix
// probeweise auf `return (w>=r)?(w-r):(MAX_RING-r+w);` zurueckgesetzt und
// diese Suite laufen lassen) schlagen beide Tests unten rot fehl --
// "Expected 3 Was 5" bzw. "Expected 1 Was 5".

static void test_bp02_tiefe_mit_loechern(void)
{
    for (int i = 0; i < 5; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0xA000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "bp02holes");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }

    // Slots 1 und 3 freigeben (simuliert erfolgreiche Retransmits/Eviction) --
    // Slots 0, 2, 4 bleiben belegt.
    ringBuffer[1][0] = 0;
    ringBuffer[3][0] = 0;

    TEST_ASSERT_EQUAL_INT(3, txRingDepth());
}

// DJ8MEH-Szenario Teil 1: ein einzelner, prioritaets-ausgehungerter Eintrag
// sitzt am Lesezeiger (iRead); alle Slots dahinter bis iWrite sind Loecher.
// Die alte Indexdistanz-Arithmetik meldete hier weiterhin die volle
// Enqueue-Anzahl (5) als Tiefe -- die ehrliche Zaehlung liefert 1.
static void test_bp02_dj8meh_szenario_prio5_am_lesezeiger(void)
{
    for (int i = 0; i < 5; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0xB000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "bp02dj8meh");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }

    // Slots 1..4 freigeben -- nur Slot 0 (am Lesezeiger) bleibt belegt, wie
    // ein prioritaets-ausgehungerter Eintrag, der von getNextTxSlot() nie
    // ausgewaehlt wird, waehrend alles dahinter laengst gesendet/verworfen ist.
    for (int i = 1; i < 5; i++)
        ringBuffer[i][0] = 0;

    TEST_ASSERT_EQUAL_INT(1, txRingDepth());
}

// --------------------------------------------------------------- BP-03
//
// txRingAgeBackground() (txring_functions.cpp): sweeps the ring and drops
// any BACKGROUND (HEY, prio 5) entry older than RING_BG_MAX_AGE_MS
// (configuration_global.h, 180000 ms). DJ8MEH-RCA Teil 2: a priority-
// starved HEY can sit unsent for many minutes (10 min in
// the field episode that motivated this) while getNextTxSlot() never picks
// it -- txRingAgeBackground() is the only place that ever frees such a
// slot, called from the main-loop 2s tick, NOT from getNextTxSlot() itself
// (Advisor F1: that path also runs on the nRF52 timer task).
//
// Kontrollrechnung (fails-before): mit einem No-Op-Rumpf von
// txRingAgeBackground() (Rueckkehr sofort nach Betreten, kein Sweep) --
// probeweise so gegen diese Suite laufen lassen -- schlagen
// test_bp03_stale_drop_und_slot_frei und
// test_bp03_rollover_sicher (zweite Haelfte) rot fehl: der Slot bleibt
// belegt (ringBuffer[slot][0] != 0 statt 0), stat_drop_count[5] bleibt 0
// statt 1. Verifiziert waehrend der Umsetzung,
// Fix danach wiederhergestellt.

static void test_bp03_stale_drop_und_slot_frei(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildHeyFrame(0xAABBCCDDUL);
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "bp03stale");
    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_BACKGROUND, ringPriority[slot]);

    // Genau ueber der Grenze (Grenze selbst ist noch KEIN Drop, siehe ">").
    mc_test_set_millis(1000UL + RING_BG_MAX_AGE_MS + 1UL);
    txRingAgeBackground((uint32_t)millis(), kNoClock);

    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT8(0, retryCount[slot]);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_BACKGROUND]);

    // Der Slot ist sofort wieder belegbar (Pool: kein Lesezeiger zum Nachziehen).
    TEST_ASSERT_EQUAL_INT(0, txRingDepth());
}

static void test_bp03_frischer_bg_bleibt(void)
{
    mc_test_set_millis(5000);
    BuiltFrame f = buildHeyFrame();
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "bp03fresh");

    // Exakt auf der Grenze (nicht darueber) -- Bedingung ist "> Grenze", also
    // bleibt der Eintrag hier noch stehen.
    mc_test_set_millis(5000UL + RING_BG_MAX_AGE_MS);
    txRingAgeBackground((uint32_t)millis(), kNoClock);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_BACKGROUND]);
}

static void test_bp03_nicht_bg_altert_nie(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildTextFrame("DK5EN-91", "Hallo"); // -> MSG_PRIO_CRITICAL
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "bp03nonbg");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);

    // Weit jenseits der BACKGROUND-Alterungsgrenze -- ein Nicht-BG-Eintrag
    // darf trotzdem nie altern.
    mc_test_set_millis(1000UL + RING_BG_MAX_AGE_MS * 100UL);
    txRingAgeBackground((uint32_t)millis(), kNoClock);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_BACKGROUND]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_CRITICAL]);
}

// Rollover-Fall (F8): ringEnqueueTime nahe UINT32_MAX, now nach dem Wrap.
// Der Rollover-sichere Cast (uint32_t)(now_ms - ringEnqueueTime[i]) muss
// sowohl "frisch bleibt" als auch "stale faellt" ueber den Wrap hinweg
// korrekt berechnen -- eine vorzeichenbehaftete oder ungecastete Differenz
// wuerde hier riesige/negative Werte liefern.
static void test_bp03_rollover_sicher(void)
{
    mc_test_set_millis(UINT32_MAX - 100UL); // 4294967195
    BuiltFrame f = buildHeyFrame(0xD00DD00DUL);
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "bp03wrap");
    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(UINT32_MAX - 100UL), ringEnqueueTime[slot]);

    // now=50 (nach dem Wrap) -> Alter = 151 ms, weit unter der Grenze: bleibt.
    mc_test_set_millis(50);
    txRingAgeBackground((uint32_t)millis(), kNoClock);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_BACKGROUND]);

    // now=179900 (weiter nach dem Wrap) -> Alter = 180001 ms > Grenze: faellt.
    mc_test_set_millis(179900);
    txRingAgeBackground((uint32_t)millis(), kNoClock);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_BACKGROUND]);
}


// --------------------------------------------------------------- TXP W1a
//
// TTL je Prioritaet (txring_ttl.h) und {CET}-Altersgate im Sweep.

static const uint32_t kCetNow = 1792843200UL; // 2026-10-24 12:00:00 UTC

static BuiltFrame buildCetFrame(uint32_t payload_utc)
{
    // UTC-Epoche -> "{CET}YYYY-MM-DD hh:mm:ss" ueber dieselbe Tageszahl-Rechnung
    uint32_t d = payload_utc / 86400UL, r = payload_utc % 86400UL;
    // civil_from_days (Hinnant)
    int32_t z = (int32_t)d + 719468;
    int32_t era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t y = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    uint32_t dd = doy - (153 * mp + 2) / 5 + 1;
    uint32_t mm = mp < 10 ? mp + 3 : mp - 9;
    if(mm <= 2) y++;
    char pl[40];
    snprintf(pl, sizeof(pl), "{CET}%04d-%02u-%02u %02u:%02u:%02u", (int)y, (unsigned)mm, (unsigned)dd,
             (unsigned)(r / 3600), (unsigned)(r % 3600 / 60), (unsigned)(r % 60));
    return buildTextFrame("*", pl, 0xCE700001UL, "OE1XXX-1");
}

static CetClock clk(bool trusted, bool ok, uint32_t now)
{
    CetClock c;
    c.trusted = trusted; c.now_ok = ok; c.now_utc = now;
    return c;
}

static void sweepAt(uint32_t t_ms) { mc_test_set_millis(t_ms); txRingAgeBackground((uint32_t)millis(), kNoClock); }

static void test_ttl_table(void)
{
    TEST_ASSERT_EQUAL_UINT32(RING_TTL_ACK_MS, txRingTtlMs(1, RING_STATUS_DONE, true));
    TEST_ASSERT_EQUAL_UINT32(0, txRingTtlMs(1, RING_STATUS_DONE, false));
    TEST_ASSERT_EQUAL_UINT32(0, txRingTtlMs(2, RING_STATUS_DONE, false));
    TEST_ASSERT_EQUAL_UINT32(RING_TTL_RELAY_MS, txRingTtlMs(3, RING_STATUS_DONE, false));
    TEST_ASSERT_EQUAL_UINT32(RING_TTL_POS_MS, txRingTtlMs(4, RING_STATUS_DONE, false));
    TEST_ASSERT_EQUAL_UINT32(RING_BG_MAX_AGE_MS, txRingTtlMs(5, RING_STATUS_DONE, false));
    TEST_ASSERT_EQUAL_UINT32(0, txRingTtlMs(1, RING_STATUS_READY, false));
    TEST_ASSERT_EQUAL_UINT32(0, txRingTtlMs(3, RING_STATUS_SENT, false));
    TEST_ASSERT_EQUAL_UINT32(0, txRingTtlMs(3, 0x14, false));
    TEST_ASSERT_EQUAL_UINT32(0, txRingTtlMs(3, RING_STATUS_EXT_PENDING, false));
    TEST_ASSERT_EQUAL_UINT32(0, txRingTtlMs(5, RING_STATUS_EXT_PENDING, false));
}

static void test_ttl_relay_300s(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildTextFrame("DK5EN-91", "relay");
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "ttl3");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_NORMAL, ringPriority[slot]);
    sweepAt(1000UL + RING_TTL_RELAY_MS);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_NORMAL]);
    sweepAt(1000UL + RING_TTL_RELAY_MS + 1);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_NORMAL]);
}

static void test_ttl_pos_120s(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildPositionFrame();
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "ttl4");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_LOW, ringPriority[slot]);
    sweepAt(1000UL + RING_TTL_POS_MS);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[slot][0]);
    sweepAt(1000UL + RING_TTL_POS_MS + 1);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
}

static void test_ttl_ack_90s(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildAckFrame();
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "ttl1");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
    sweepAt(1000UL + RING_TTL_ACK_MS);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[slot][0]);
    sweepAt(1000UL + RING_TTL_ACK_MS + 1);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_CRITICAL]);
}

static void test_ttl_prio5_180s(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildHeyFrame();
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "ttl5");
    sweepAt(1000UL + 180000UL);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[slot][0]);
    sweepAt(1000UL + 180001UL);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_BACKGROUND]);
}

static void test_ttl_own_text_never_ages(void)
{
    mc_test_set_millis(1000);
    BuiltFrame a = buildTextFrame("DK5EN-91", "dm", 0x1001UL);
    BuiltFrame b = buildTextFrame("*", "bc", 0x1002UL);
    int sa = addTxRingEntry(a.bytes, a.len, RING_STATUS_READY, "ownR");
    int sb = addTxRingEntry(b.bytes, b.len, RING_STATUS_SENT, "ownS");
    sweepAt(1000UL + 3600000UL);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[sa][0]);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[sb][0]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_CRITICAL]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_HIGH]);
}

static void test_ttl_prio2_done_never_ages(void)
{
    mc_test_set_millis(1000);
    // Gruppe/Broadcast mit Status DONE gilt als Relay (prio 3); prio 2 haengt an READY/SENT
    BuiltFrame b = buildTextFrame("*", "bc", 0x1003UL);
    int sb = addTxRingEntry(b.bytes, b.len, RING_STATUS_READY, "p2");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_HIGH, ringPriority[sb]);
    sweepAt(1000UL + 3600000UL);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[sb][0]);
}

// W1a-Advisor: eigene Einmal-Texte (addTxRingEntryOnce: {ping}, RM1-Antwort,
// Mailbox-Zustellung) landen als prio 1/2 mit Status DONE. D4: nur ACKs haben
// 90 s, eigene DMs und Gruppentexte keine TTL.
static void test_ttl_own_once_dm_never_ages(void)
{
    mc_test_set_millis(1000);
    BuiltFrame b = buildTextFrame("DK5EN-1", "{ping}", 0x1004UL);
    int s = addTxRingEntryOnce(b.bytes, b.len, "ping");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[s]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, (uint8_t)ringBuffer[s][1]);
    sweepAt(1000UL + 3600000UL);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[s][0]);
}

static void test_ttl_own_once_group_never_ages(void)
{
    mc_test_set_millis(1000);
    BuiltFrame b = buildTextFrame("*", "{MCP}x", 0x1005UL);
    int s = addTxRingEntryOnce(b.bytes, b.len, "mcp");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_HIGH, ringPriority[s]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, (uint8_t)ringBuffer[s][1]);
    sweepAt(1000UL + 3600000UL);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[s][0]);
}

static void test_ttl_own_text_ack_ages_90s(void)
{
    mc_test_set_millis(1000);
    BuiltFrame b = buildTextFrame("DK5EN-1", "DK5EN-1  :ack123", 0x1006UL);
    int s = addTxRingEntryOnce(b.bytes, b.len, "ack");
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[s]);
    sweepAt(1000UL + RING_TTL_ACK_MS - 1000UL);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[s][0]);
    sweepAt(1000UL + RING_TTL_ACK_MS + 1000UL);
    TEST_ASSERT_EQUAL(0, ringBuffer[s][0]);
}

static void test_ttl_ack_detection(void)
{
    BuiltFrame a = buildTextFrame("DK5EN-1", "DK5EN-1  :ack123");
    BuiltFrame w = buildTextFrame("WLNK-1", "ack0042");
    BuiltFrame d = buildTextFrame("DK5EN-1", "ackermann");
    BuiltFrame g = buildTextFrame("DK5EN-1", "hallo");
    BuiltFrame b = buildAckFrame();
    TEST_ASSERT_TRUE(txRingFrameIsAck(a.bytes, a.len));
    TEST_ASSERT_TRUE(txRingFrameIsAck(w.bytes, w.len));
    TEST_ASSERT_TRUE(txRingFrameIsAck(b.bytes, b.len));
    TEST_ASSERT_FALSE(txRingFrameIsAck(d.bytes, d.len));
    TEST_ASSERT_FALSE(txRingFrameIsAck(g.bytes, g.len));
}

#if defined(EXTERNAL_RADIO)
static void test_ttl_ext_pending_never_ages(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildHeyFrame();
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "ext");
    ringBuffer[slot][1] = RING_STATUS_EXT_PENDING;
    sweepAt(1000UL + 3600000UL);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[slot][0]);
}
#endif

static void cetSweepCase(uint32_t payload_off_back, bool trusted, bool ok, bool expect_dropped, int32_t shift = 0)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildCetFrame((uint32_t)((int32_t)kCetNow - (int32_t)payload_off_back + shift));
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "cet");
    TEST_ASSERT_GREATER_OR_EQUAL_INT(0, slot);
    mc_test_set_millis(2000); // weit unter jeder ms-TTL
    txRingAgeBackground((uint32_t)millis(), clk(trusted, ok, kCetNow));
    if(expect_dropped)
    {
        TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
        TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_NORMAL]);
    }
    else
    {
        TEST_ASSERT_NOT_EQUAL(0, ringBuffer[slot][0]);
        TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_NORMAL]);
    }
}

static void test_cet_fresh_kept(void)      { cetSweepCase(60, true, true, false); }
static void test_cet_151s_dropped(void)    { cetSweepCase(151, true, true, true); }
static void test_cet_150s_kept(void)       { cetSweepCase(150, true, true, false); }
static void test_cet_untrusted_dropped(void) { cetSweepCase(10, false, true, true); }
static void test_cet_no_now_dropped(void)  { cetSweepCase(10, true, false, true); }
static void test_cet_future_dropped(void)  { cetSweepCase(0, true, true, true, 200); }

static void test_cet_drop_only_sweep(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildCetFrame(kCetNow - 400);
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "cetonly");
    BuiltFrame g = buildTextFrame("DK5EN-91", "r", 0x2001UL);
    int s2 = addTxRingEntry(g.bytes, g.len, RING_STATUS_DONE, "keep");
    txRingDropStaleCet(clk(true, true, kCetNow));
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[slot][0]);
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[s2][0]);
}

static void test_cet_non_cet_text_untrusted_kept(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildTextFrame("DK5EN-91", "hallo CET egal", 0x3001UL);
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "noncet");
    mc_test_set_millis(2000);
    txRingAgeBackground((uint32_t)millis(), clk(false, false, 0));
    TEST_ASSERT_NOT_EQUAL(0, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_NORMAL]);
}

// --------------------------------------------------------------- WQ-01
//
// txRingPrioCounts(): per-priority Belegungszaehlung fuers Web-GUI-
// Queue-Panel, gleicher lock-freier Scan wie txRingDepth() (siehe dessen
// Testabdeckung oben fuer die Loecher-Semantik), nur zusaetzlich pro
// Prioritaet aufgeschluesselt.

static void test_wq01_leerer_ring_liefert_nur_nullen(void)
{
    uint8_t out[6];
    memset(out, 0xFF, sizeof(out)); // Alt-Muell, damit ein fehlendes Zero-Init auffiele

    txRingPrioCounts(out);

    for (int i = 0; i < 6; i++)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, out[i], "leerer Ring muss ueberall 0 liefern");
}

// Je ein Eintrag pro Prioritaetsstufe (CRITICAL..BACKGROUND) -- out[0] muss
// mit txRingDepth() uebereinstimmen, out[1..5] je genau 1 zaehlen.
static void test_wq01_gemischte_prioritaeten_stimmen_mit_klassifizierung_ueberein(void)
{
    BuiltFrame ack = buildAckFrame();                          // CRITICAL (1)
    BuiltFrame dm  = buildTextFrame("DK5EN-91", "dringend");   // CRITICAL (1) -- Text-DM
    BuiltFrame grp = buildTextFrame("9999", "Gruppen-Text");   // HIGH (2)
    BuiltFrame rel = buildTextFrame("DK5EN-91", "Relay");      // NORMAL (3), ueber RING_STATUS_DONE
    BuiltFrame pos = buildPositionFrame();                     // LOW (4)
    BuiltFrame hey = buildHeyFrame();                          // BACKGROUND (5)

    TEST_ASSERT_TRUE(addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "wq01") >= 0);
    TEST_ASSERT_TRUE(addTxRingEntry(dm.bytes, dm.len, RING_STATUS_READY, "wq01") >= 0);
    TEST_ASSERT_TRUE(addTxRingEntry(grp.bytes, grp.len, RING_STATUS_READY, "wq01") >= 0);
    TEST_ASSERT_TRUE(addTxRingEntry(rel.bytes, rel.len, RING_STATUS_DONE, "wq01") >= 0);
    TEST_ASSERT_TRUE(addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "wq01") >= 0);
    TEST_ASSERT_TRUE(addTxRingEntry(hey.bytes, hey.len, RING_STATUS_READY, "wq01") >= 0);

    uint8_t out[6];
    txRingPrioCounts(out);

    TEST_ASSERT_EQUAL_INT(txRingDepth(), out[0]);
    TEST_ASSERT_EQUAL_UINT8(6, out[0]);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, out[MSG_PRIO_CRITICAL], "ACK + Text-DM");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, out[MSG_PRIO_HIGH], "Gruppen-Text");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, out[MSG_PRIO_NORMAL], "Relay");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, out[MSG_PRIO_LOW], "Position");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, out[MSG_PRIO_BACKGROUND], "HEY");
}

// Loecher-Szenario (wie test_bp02_tiefe_mit_loechern): ein in der Mitte
// freigegebener Slot darf weder in out[0] noch in seiner Prio-Bucket
// mitgezaehlt werden.
static void test_wq01_loch_in_der_mitte_wird_nicht_mitgezaehlt(void)
{
    for (int i = 0; i < 5; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0xC000 + i)); // LOW
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "wq01holes");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }

    // Slots 1 und 3 freigeben -- Slots 0, 2, 4 bleiben belegt (alle LOW).
    ringBuffer[1][0] = 0;
    ringBuffer[3][0] = 0;

    uint8_t out[6];
    txRingPrioCounts(out);

    TEST_ASSERT_EQUAL_INT(txRingDepth(), out[0]);
    TEST_ASSERT_EQUAL_UINT8(3, out[0]);
    TEST_ASSERT_EQUAL_UINT8(3, out[MSG_PRIO_LOW]);
    TEST_ASSERT_EQUAL_UINT8(0, out[MSG_PRIO_CRITICAL]);
    TEST_ASSERT_EQUAL_UINT8(0, out[MSG_PRIO_HIGH]);
    TEST_ASSERT_EQUAL_UINT8(0, out[MSG_PRIO_NORMAL]);
    TEST_ASSERT_EQUAL_UINT8(0, out[MSG_PRIO_BACKGROUND]);
}

// EXT_PENDING-Guard (F7): nur relevant/baubar unter EXTERNAL_RADIO, das
// env:native_aprs NICHT setzt (siehe platformio.ini [env:native_aprs] --
// build_flags definiert dort kein -D EXTERNAL_RADIO=1, anders als
// [env:*_external_radio]). Ein #if-gated Test wuerde in diesem Env nie
// laufen (toter Test) -- deshalb hier bewusst ausgelassen, siehe Wave-Report.

// ----------------------------------- Nachbarschaftsmatrix Stufe 2, Fix vom
// -------------------------------------------------- Feldlauf 23.09.2026
//
// Feldbefund (DK5EN-98, --nbrrelay on, 9h): der Fall-B-Nachrang
// (NBR_RELAY_CASE_B_EXTRA_MS) wurde bei JEDEM CSMA-Re-Arm (jedes empfangene
// Frame) neu auf die Basis addiert statt nur EINMAL ab dem Einreihen zu
// gelten -- ein Fall-B-Relay an der Ringspitze wartete dadurch effektiv auf
// eine durchgehende Funkstille (Median 137s, Maximum 16min), und
// getNextTxSlot() kannte den Fall gar nicht: alles dahinter (auch Fall-A-
// Relays, eigene Sendungen, HN-Meldungen) wartete mit. Ergebnis: 149
// verworfene Relays + 10 eigene HN-Meldungen in 9h (RING_DROP_NEW/
// RING_DROP_STALE), 0 im reinen --nbrrelay count.
//
// Die folgenden Tests fixieren den GEFIXTEN Zustand (einmalige Deadline ab
// Einreihen, dreiphasiger Fall-B-Backoff, Fall-B-Hold blockiert
// getNextTxSlot() nicht mehr) und dokumentieren je die genaue Assertion, die
// gegen den ALTEN Code (bei jedem Aufruf erneut EXTRA addieren, kein
// Hold-Wissen in getNextTxSlot()) rot faellt.

// Test 1: ein Fall-B-Relay (ringAlone==0) vor einem Fall-A-Relay
// (ringAlone!=0) derselben Prioritaet (beide POS, also LOW) -- waehrend der
// Fall-B-Sperre gewinnt Fall A (obwohl B zuerst eingereiht wurde und bei
// gleicher Prio sonst FIFO gilt), nach Ablauf der Sperre gewinnt wieder B
// (FIFO, B war zuerst da). ALTER CODE (kein Hold-Wissen in getNextTxSlot()):
// die erste Assertion (waehrend der Sperre) faellt rot -- getNextTxSlot()
// haette B (FIFO-Erster) zurueckgegeben, nicht A.
static void test_nbr_caseb_hold_weicht_fall_a_gleicher_prio(void)
{
    bNBRCANCEL = true;
    mc_test_set_millis(1000);

    BuiltFrame posB = buildPositionFrame(0xB001UL); // LOW
    int slotB = addTxRingEntry(posB.bytes, posB.len, RING_STATUS_DONE, "rx_relay",
                                0, true, RING_KIND_RELAY, /*need*/0, /*alone*/0); // Fall B
    TEST_ASSERT_EQUAL_INT(0, slotB);

    BuiltFrame posA = buildPositionFrame(0xA001UL); // LOW, gleiche Prio wie B
    NbrMask aloneA = nbrMaskBit(0); // irgendein gesetztes Bit -- nur "nicht leer" zaehlt fuer Fall A
    int slotA = addTxRingEntry(posA.bytes, posA.len, RING_STATUS_DONE, "rx_relay",
                                0, true, RING_KIND_RELAY, /*need*/nullptr, /*alone*/&aloneA); // Fall A
    TEST_ASSERT_EQUAL_INT(1, slotA);

    // Innerhalb der Fall-B-Sperre (waited=0 < NBR_RELAY_CASE_B_EXTRA_MS):
    // Fall A ist der einzige NICHT gehaltene Kandidat und gewinnt, trotz
    // gleicher Prio und spaeterer Einreihung.
    TEST_ASSERT_EQUAL_INT_MESSAGE(slotA, getNextTxSlot(), "waehrend der Fall-B-Sperre muss Fall A gewinnen");

    // Nach Ablauf der Sperre sind beide nicht mehr gehalten -> normales
    // Prio+FIFO greift wieder, B (zuerst eingereiht) gewinnt.
    mc_test_set_millis(1000UL + NBR_RELAY_CASE_B_EXTRA_MS + 1UL);
    TEST_ASSERT_EQUAL_INT_MESSAGE(slotB, getNextTxSlot(), "nach der Sperre muss B (FIFO-Erster) gewinnen");
}

// Test 2: ein gehaltener Fall-B-Relay (hier: Positions-Relay, Prio LOW=4,
// also "bessere" Prio-Nummer als die HN-Meldung) darf eine spaeter
// eingereihte, nominell schlechter priorisierte eigene HN-Meldung
// (HEY-Frame, RING_KIND_OTHER, Prio BACKGROUND=5) NICHT blockieren -- genau
// das Feldsymptom ("10 eigene HN-Meldungen in 9h verhungert"). ALTER CODE:
// getNextTxSlot() kennt den Hold nicht und waehlt stur nach Prio -> haette
// den ACK-Relay-Slot zurueckgegeben (Prio 1 < 5), diese Assertion faellt rot.
static void test_nbr_caseb_hold_blockiert_hn_meldung_nicht(void)
{
    bNBRCANCEL = true;
    mc_test_set_millis(2000);

    // Positions-Relay (LOW, bessere Prio als die HN-Meldung) in Fall B -- genau
    // die Lage aus dem Feldlauf 23.09. (ACK-Relays bekommen nie RING_KIND_RELAY,
    // nur Text/POS/HEY laufen durch nbrRelayNeed()).
    BuiltFrame posRelay = buildPositionFrame(0xB002UL);
    int slotHeld = addTxRingEntry(posRelay.bytes, posRelay.len, RING_STATUS_DONE, "rx_relay",
                                   0, true, RING_KIND_RELAY, /*need*/0, /*alone*/0); // Fall B -> gehalten
    TEST_ASSERT_EQUAL_INT(0, slotHeld);

    BuiltFrame heyOwn = buildHeyFrame(0xC002UL); // BACKGROUND -- eigene HN-Meldung, kind=OTHER (Default)
    int slotHN = addTxRingEntry(heyOwn.bytes, heyOwn.len, RING_STATUS_READY, "nbr_report");
    TEST_ASSERT_EQUAL_INT(1, slotHN);

    TEST_ASSERT_TRUE(txringInCaseBHold(slotHeld, (uint32_t)millis()));
    TEST_ASSERT_FALSE(txringInCaseBHold(slotHN, (uint32_t)millis()));

    TEST_ASSERT_EQUAL_INT_MESSAGE(slotHN, getNextTxSlot(),
        "gehaltener Fall-B-Relay darf die spaeter eingereihte HN-Meldung nicht blockieren");
}

// Test 3: dreiphasiger Fall-B-Backoff direkt gegen txringCaseBackoffSlot()
// (LOW-Prio-Slot, CSMA_PRIO_BASE_4=5500, attempt=0 -- keine Versuchs-
// Skalierung). Normale Fall-B-Basis (kein EXTRA): 5500 + (7..9)*35 =
// [5745,5815] (NBR_RELAY_CASE_B_SLOT_START=7 + random(0,3)={0,1,2}).
//
// waited=5000 (< EXTRA=20000): Rest-Hold (20000-5000=15000) > normale Basis
// (max. 5815) -> Ergebnis GENAU 15000, kein Zufallsanteil. ALTER CODE
// (Basis+EXTRA+Slots bei jedem Aufruf, unabhaengig von waited) haette hier
// ~25745..25815 geliefert -- die Obergrenze 16000 faellt dagegen rot.
//
// waited=25000 (>= EXTRA, < MAX_WAIT=60000): normale Basis OHNE EXTRA,
// [5745,5815]. ALTER CODE haette weiterhin ~25745..25815 geliefert -- die
// Obergrenze 5815 faellt dagegen rot.
//
// waited=65000 (>= MAX_WAIT=60000, neuer 60s-Deckel): Kurzsuche wie Fall A,
// NBR_RELAY_CASE_A_SHORT_MS(150) + random(0,3)*35 = [150,220]. ALTER CODE
// kannte MAX_WAIT gar nicht -- haette weiterhin ~25745..25815 geliefert, die
// Obergrenze 220 faellt dagegen rot.
static void test_nbr_caseb_backoff_dreiphasig(void)
{
    ringPriority[0] = MSG_PRIO_LOW;
    ringAlone[0] = nbrMaskNone(); // Fall B

    unsigned long b1 = txringCaseBackoffSlot(0, /*attempt*/0, /*now_ms*/5000UL);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(15000UL, b1, "Rest-Hold bei waited=5s");

    unsigned long b2 = txringCaseBackoffSlot(0, /*attempt*/0, /*now_ms*/25000UL);
    TEST_ASSERT_TRUE_MESSAGE(b2 >= 5745UL && b2 <= 5815UL, "normale Fall-B-Basis ohne EXTRA bei waited=25s");

    unsigned long b3 = txringCaseBackoffSlot(0, /*attempt*/0, /*now_ms*/65000UL);
    TEST_ASSERT_TRUE_MESSAGE(b3 >= 150UL && b3 <= 220UL, "Kurzsuche wie Fall A ab dem 60s-Deckel");
}

// Test 4: Re-Arm-Invarianz -- zwei Aufrufe fuer DENSELBEN Slot, 50ms
// auseinander (wie zwei kurz aufeinanderfolgende CSMA-Re-Arms durch zwei
// empfangene Frames), duerfen die Sperre NICHT neu starten. Bei
// waited=3000/3050 (beide < EXTRA=20000, Rest-Hold jeweils weit ueber der
// normalen Basis) ist das Ergebnis exakt EXTRA-waited, ohne Zufallsanteil --
// exakt pruefbar. ALTER CODE addierte EXTRA bei jedem Aufruf neu und war von
// waited komplett unabhaengig: beide Aufrufe haetten denselben, viel
// groesseren Wert (~25745..25815) geliefert -- diese beiden Assertions
// fallen einzeln schon rot (siehe Test 3), hier zusaetzlich fixiert, dass
// der zweite Aufruf die Sperre um genau das Delta (50ms) verkuerzt statt sie
// zu verlaengern/neu zu starten.
static void test_nbr_caseb_kein_re_arm(void)
{
    ringPriority[0] = MSG_PRIO_LOW;
    ringAlone[0] = nbrMaskNone(); // Fall B

    unsigned long first = txringCaseBackoffSlot(0, /*attempt*/0, /*now_ms*/3000UL);
    unsigned long second = txringCaseBackoffSlot(0, /*attempt*/0, /*now_ms*/3050UL);

    TEST_ASSERT_EQUAL_UINT32(17000UL, first);
    TEST_ASSERT_EQUAL_UINT32(16950UL, second);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(50UL, first - second,
        "die Sperre muss um genau das Zeitdelta zwischen den Re-Arms schrumpfen, nicht neu starten");
}

// Test 5: ohne bNBRCANCEL (--nbrrelay off/count) bleibt alles unveraendert
// -- txringInCaseBHold() liefert immer false, egal wie kind/alone stehen,
// und getNextTxSlot() waehlt reines Prio+FIFO wie vor diesem Fix. ALTER CODE
// hatte diesen Codepfad ueberhaupt nicht -- dieser Test fixiert nur, dass
// der NEUE Code ihn ebenfalls nicht aendert.
static void test_nbr_ohne_bnbrcancel_unveraendert(void)
{
    // bNBRCANCEL bleibt false (Default aus resetRing()).
    mc_test_set_millis(3000);

    BuiltFrame posLow = buildPositionFrame(0xB005UL); // LOW, kind/alone wie ein Fall-B-Relay, aber bNBRCANCEL aus
    int slotLow = addTxRingEntry(posLow.bytes, posLow.len, RING_STATUS_DONE, "rx_relay",
                                  0, true, RING_KIND_RELAY, /*need*/0, /*alone*/0);
    TEST_ASSERT_EQUAL_INT(0, slotLow);

    BuiltFrame heyBg = buildHeyFrame(0xC005UL); // BACKGROUND, schlechtere Prio
    int slotBg = addTxRingEntry(heyBg.bytes, heyBg.len, RING_STATUS_READY, "nbr_report");
    TEST_ASSERT_EQUAL_INT(1, slotBg);

    TEST_ASSERT_FALSE(txringInCaseBHold(slotLow, (uint32_t)millis()));
    // Reines Prio+FIFO: LOW (4) schlaegt BACKGROUND (5), wie vor diesem Fix.
    TEST_ASSERT_EQUAL_INT(slotLow, getNextTxSlot());
}

// Test 6: eine Text-Relay-Nachricht bleibt vom Fall-B-Hold komplett
// unberuehrt (Konzept 5.1: Menschen warten darauf), SELBST wenn ringAlone==0
// und RING_KIND_RELAY gesetzt sind. Realistische Probe (Advisor 2026-09-23;
// ACK-Frames bekommen in der Firmware nie RING_KIND_RELAY): Text-Relay
// (NORMAL=3) vor einer eigenen, NICHT gehaltenen Position (RING_KIND_OTHER,
// LOW=4). Richtig klassifiziert gewinnt der Text nach Prio; waere er
// faelschlich gehalten, gewaenne die Position als einziger nicht gehaltener
// Kandidat.
static void test_nbr_caseb_text_relay_kein_hold(void)
{
    bNBRCANCEL = true;
    mc_test_set_millis(4000);

    BuiltFrame textRelay = buildTextFrame("DK5EN-91", "Hallo", 0xE006UL, "DK5EN-90");
    int slotText = addTxRingEntry(textRelay.bytes, textRelay.len, RING_STATUS_DONE, "rx_relay",
                                   0, true, RING_KIND_RELAY, /*need*/0, /*alone*/0); // alone=0, waere Fall B ohne den Text-Ausschluss
    TEST_ASSERT_EQUAL_INT(0, slotText);

    BuiltFrame ownPos = buildPositionFrame(0xD006UL);
    int slotPos = addTxRingEntry(ownPos.bytes, ownPos.len, RING_STATUS_READY, "own_pos");
    TEST_ASSERT_EQUAL_INT(1, slotPos);

    TEST_ASSERT_FALSE_MESSAGE(txringInCaseBHold(slotText, (uint32_t)millis()), "Text-Relay darf nie gehalten sein");
    TEST_ASSERT_EQUAL_INT_MESSAGE(slotText, getNextTxSlot(),
        "Text-Relay (NORMAL) muss vor der eigenen Position (LOW) gewinnen -- er ist nicht gehalten");
}

// --------------------------------------------------------------- M0-1 (0.4)
//
// ringstat_parked_overwrite (docs/dm-transport-impl-plan-20260913.md). Im
// alten Ring: ein Enqueue landete auf einem retransmit-pending Slot (Laenge
// != 0, Status weder READY noch DONE noch EXT_PENDING). Im Slot-Pool (TXP
// W1b) kann das nur noch ueber die Eviction passieren: der verdraengte
// Eintrag war ein geparkter SENT-Slot (0x01..0x14-Alterungsfolge aus
// updateRetransmissionStatus(), hier 0x05) und nicht READY/DONE. Ein freier
// Slot zaehlt nicht. ringstat_enqueue zaehlt jeden erreichten Write.

// Pool voll mit LOW-Positionen (READY); Slot 0 ist der aelteste (gleiche
// Zeit, kleinster Index gewinnt den Gleichstand).
static void fillPoolWithLowPositions(uint32_t idBase)
{
    for (int i = 0; i < MAX_RING; i++)
    {
        BuiltFrame f = buildPositionFrame(idBase + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "m01fill"));
    }
    ringstat_enqueue.store(0);
}

static void test_ringstat_parked_overwrite_bei_pending_slot(void)
{
    fillPoolWithLowPositions(0xF000UL);
    ringBuffer[0][1] = 0x05;   // Slot 0: geparkt, unbestaetigter Sendeversuch

    BuiltFrame ack = buildAckFrame(0xF001UL);   // CRITICAL verdraengt den aeltesten LOW
    int slot = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "parked_ovw");

    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT32(1, ringstat_parked_overwrite.load());
    TEST_ASSERT_EQUAL_UINT32(1, ringstat_enqueue.load());
}

static void test_ringstat_parked_overwrite_bei_leerem_slot_bleibt_null(void)
{
    // resetRing() (per setUp bereits gelaufen) liefert einen frischen, leeren
    // Pool -- kein Ueberschreiben eines Pending-Eintrags.
    BuiltFrame f = buildPositionFrame(0xF002UL);
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "parked_empty");

    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT32(0, ringstat_parked_overwrite.load());
    TEST_ASSERT_EQUAL_UINT32(1, ringstat_enqueue.load());
}

// Die Ausschluesse einzeln: wird ein READY- oder DONE-Slot verdraengt, zaehlt
// das nicht als geparkter Eintrag; ein EXT_PENDING-Slot wird gar nicht
// verdraengt (die Eviction nimmt den naechstaeltesten, Zaehler bleibt 0).
static void test_ringstat_parked_overwrite_ignoriert_done_ready_ext(void)
{
    const uint8_t statusse[3] = { RING_STATUS_DONE, RING_STATUS_READY, RING_STATUS_EXT_PENDING };
    for(int i = 0; i < 3; i++)
    {
        resetRing();
        fillPoolWithLowPositions(0xF010UL);
        ringBuffer[0][1] = statusse[i];

        BuiltFrame ack = buildAckFrame(0xF020UL + (uint32_t)i);
        int slot = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "parked_excl");

        TEST_ASSERT_EQUAL_INT(statusse[i] == RING_STATUS_EXT_PENDING ? 1 : 0, slot);
        TEST_ASSERT_EQUAL_UINT32(0, ringstat_parked_overwrite.load());
        TEST_ASSERT_EQUAL_UINT32(1, ringstat_enqueue.load());
        if (statusse[i] == RING_STATUS_EXT_PENDING)
            TEST_ASSERT_EQUAL_UINT8(RING_STATUS_EXT_PENDING, ringBuffer[0][1]); // unangetastet
    }
}

// --------------------------------------------------------------- TXP W1b
//
// Slot-Pool: freie Slots werden wiederverwendet, Kapazitaet ist MAX_RING,
// Auswahl nach Prio und Enqueue-Alter, INFLIGHT-Fenster, Gauge-Zaehler.

// Ein geparkter eigener Text (SENT) mitten im vollen Pool blockiert nichts:
// Loecher dahinter UND davor werden belegt. (Im alten Ring scheiterte das mit
// RING_DROP_NEW, sobald der Parker am Lesezeiger sass und der Schreibzeiger
// ihn eingeholt hatte.)
static void test_pool_loch_hinter_geparktem_sent_slot_wird_gefuellt(void)
{
    // Slot 0: eigener Text, per doTX-Aequivalent als SENT geparkt (len bleibt).
    BuiltFrame txt = buildTextFrame("DK5EN-91", "park me", 0xC100UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(txt.bytes, txt.len, RING_STATUS_READY, "user_msg"));
    uint8_t prev = 0xEE;
    TEST_ASSERT_TRUE(txRingBeginTx(0, &prev));
    txRingFinishTx(0, prev, TXRING_FIN_SENT);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_SENT, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)txt.len, ringBuffer[0][0]);

    // Rest des Pools mit Relays fuellen (voll), dann zwei Loecher reissen.
    for (int i = 1; i < MAX_RING; i++)
    {
        BuiltFrame f = buildTextFrame("DK5EN-92", "relay", 0xC200UL + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_DONE, "rx_relay"));
    }
    ringBuffer[7][0] = 0;
    ringBuffer[13][0] = 0;

    BuiltFrame a = buildTextFrame("DK5EN-92", "neu1", 0xC300UL);
    BuiltFrame b = buildTextFrame("DK5EN-92", "neu2", 0xC301UL);
    TEST_ASSERT_EQUAL_INT(7, addTxRingEntry(a.bytes, a.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(13, addTxRingEntry(b.bytes, b.len, RING_STATUS_DONE, "rx_relay"));

    // Der Parker ist unberuehrt; kein Drop gebucht.
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_SENT, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_NORMAL]);
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());
}

// Kapazitaet: MAX_RING Eintraege passen (der alte Ring nahm MAX_RING-1), der
// naechste gleichrangige wird verworfen.
static void test_pool_kapazitaet_ist_max_ring(void)
{
    TEST_ASSERT_EQUAL_INT(20, MAX_RING);
    for (int i = 0; i < MAX_RING; i++)
    {
        BuiltFrame f = buildPositionFrame(0xC400UL + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "cap"));
    }
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());

    BuiltFrame extra = buildPositionFrame(0xC4FFUL);
    TEST_ASSERT_EQUAL_INT(RING_ENQUEUE_FULL, addTxRingEntry(extra.bytes, extra.len, RING_STATUS_READY, "cap"));
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());
}

// Gleiche Prio: der AELTERE Eintrag (ringEnqueueTime) gewinnt, auch wenn er
// auf dem hoeheren Index sitzt; wrap-sicher ueber den uint32-Ueberlauf.
static void test_pool_gleiche_prio_aelterer_eintrag_zuerst_auch_bei_hoeherem_index(void)
{
    mc_test_set_millis(10);
    BuiltFrame a = buildPositionFrame(0xC501UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(a.bytes, a.len, RING_STATUS_READY, "tie"));
    mc_test_set_millis(20);
    BuiltFrame b = buildPositionFrame(0xC502UL);
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(b.bytes, b.len, RING_STATUS_READY, "tie"));

    ringBuffer[0][0] = 0;   // A gesendet, Slot 0 frei
    mc_test_set_millis(30);
    BuiltFrame c = buildPositionFrame(0xC503UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(c.bytes, c.len, RING_STATUS_READY, "tie")); // erster freier Slot

    // B (Zeit 20, Slot 1) ist aelter als C (Zeit 30, Slot 0).
    TEST_ASSERT_EQUAL_INT(1, getNextTxSlot());

    // Wrap: Slot 0 jetzt "nach dem Ueberlauf" (klein), Slot 1 davor (gross)
    // -> Slot 1 bleibt der aeltere.
    ringEnqueueTime[0] = 5UL;
    ringEnqueueTime[1] = UINT32_MAX - 5UL;
    TEST_ASSERT_EQUAL_INT(1, getNextTxSlot());
    // und umgekehrt
    ringEnqueueTime[0] = UINT32_MAX - 5UL;
    ringEnqueueTime[1] = 5UL;
    TEST_ASSERT_EQUAL_INT(0, getNextTxSlot());

    // Prio schlaegt Alter: ein juengerer ACK gewinnt gegen beide Positionen.
    mc_test_set_millis(99);
    BuiltFrame ack = buildAckFrame(0xC504UL);
    int sAck = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "tie");
    TEST_ASSERT_EQUAL_INT(sAck, getNextTxSlot());
}

// INFLIGHT-Fenster: ein von doTX() gegriffener Slot behaelt seine Laenge,
// wird nicht erneut gewaehlt, nicht verdraengt und nicht von einem Enqueue
// ueberschrieben; Rollback stellt READY bzw. DONE wieder her.
static void test_pool_inflight_slot_ist_geschuetzt_und_rollback_stellt_status_her(void)
{
    // Slot 0: LOW (schlechteste Prio -> waere der erste Eviction-Kandidat),
    // alle anderen CRITICAL (ACK).
    BuiltFrame pos = buildPositionFrame(0xC600UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "inflt"));
    for (int i = 1; i < MAX_RING; i++)
    {
        BuiltFrame f = buildAckFrame(0xC610UL + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "inflt"));
    }

    uint8_t prev = 0xEE;
    TEST_ASSERT_TRUE(txRingBeginTx(0, &prev));
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_READY, prev);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_INFLIGHT, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)pos.len, ringBuffer[0][0]);   // Laenge bleibt
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());
    // zweites Begin auf demselben Slot scheitert
    uint8_t prev2 = 0xEE;
    TEST_ASSERT_FALSE(txRingBeginTx(0, &prev2));

    // Auswahl sieht den INFLIGHT-Slot nicht (der ACK-Slot 1 gewinnt).
    TEST_ASSERT_EQUAL_INT(1, getNextTxSlot());

    // Ein weiterer ACK wuerde den LOW-Slot 0 verdraengen -- ist er INFLIGHT,
    // wird der Neue stattdessen verworfen und Slot 0 bleibt unversehrt.
    BuiltFrame ack = buildAckFrame(0xC6FFUL);
    TEST_ASSERT_EQUAL_INT(RING_ENQUEUE_FULL, addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "inflt"));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)pos.len, ringBuffer[0][0]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_INFLIGHT, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pos.bytes, &ringBuffer[0][2], pos.len);

    // Rollback: READY zurueck, Laenge unveraendert, wieder waehlbar nach ACKs.
    txRingFinishTx(0, prev, TXRING_FIN_ROLLBACK);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_READY, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)pos.len, ringBuffer[0][0]);
    // Doppeltes Finish ist ein No-Op (Slot ist nicht mehr INFLIGHT).
    txRingFinishTx(0, RING_STATUS_DONE, TXRING_FIN_DROP);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)pos.len, ringBuffer[0][0]);

    // Rollback eines Einmal-Slots (DONE) stellt DONE her.
    resetRing();
    BuiltFrame once = buildPositionFrame(0xC700UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(once.bytes, once.len, RING_STATUS_DONE, "once"));
    TEST_ASSERT_TRUE(txRingBeginTx(0, &prev));
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, prev);
    txRingFinishTx(0, prev, TXRING_FIN_ROLLBACK);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)once.len, ringBuffer[0][0]);
}

// Ausgaenge von doTX(): Einmal-Slot (DONE) -> frei; eigener Text (READY) ->
// SENT mit Laenge (Retry-Leiter); READY-Nicht-Text -> frei; Drop -> frei.
// Begin auf freiem oder geparktem Slot scheitert.
static void test_pool_finish_tx_ausgaenge(void)
{
    uint8_t prev = 0;

    // Einmal (DONE) gesendet -> frei
    BuiltFrame d = buildTextFrame("DK5EN-91", "once", 0xC800UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntryOnce(d.bytes, d.len, "ack_once"));
    TEST_ASSERT_TRUE(txRingBeginTx(0, &prev));
    txRingFinishTx(0, prev, TXRING_FIN_SENT);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]);

    // Eigener Text (READY) gesendet -> SENT geparkt, Laenge bleibt
    BuiltFrame t = buildTextFrame("DK5EN-91", "own", 0xC801UL);
    int st = addTxRingEntry(t.bytes, t.len, RING_STATUS_READY, "user_msg");
    TEST_ASSERT_EQUAL_INT(0, st);
    TEST_ASSERT_TRUE(txRingBeginTx(st, &prev));
    txRingFinishTx(st, prev, TXRING_FIN_SENT);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_SENT, ringBuffer[st][1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)t.len, ringBuffer[st][0]);
    TEST_ASSERT_EQUAL_INT(-1, getNextTxSlot());   // SENT wird nicht gewaehlt
    TEST_ASSERT_FALSE(txRingBeginTx(st, &prev));  // geparkt: nicht greifbar
    TEST_ASSERT_FALSE(txRingBeginTx(5, &prev));   // frei: nicht greifbar
    TEST_ASSERT_FALSE(txRingBeginTx(-1, &prev));
    TEST_ASSERT_FALSE(txRingBeginTx(MAX_RING, &prev));

    // READY-Nicht-Text (Position) gesendet -> frei
    BuiltFrame p = buildPositionFrame(0xC802UL);
    int sp = addTxRingEntry(p.bytes, p.len, RING_STATUS_READY, "user_pos");
    TEST_ASSERT_TRUE(txRingBeginTx(sp, &prev));
    txRingFinishTx(sp, prev, TXRING_FIN_SENT);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[sp][0]);

    // Verworfen ohne Senden (TX aus / unkonfiguriert / Decode-Fehler) -> frei
    BuiltFrame t2 = buildTextFrame("DK5EN-91", "drop", 0xC803UL);
    int s2 = addTxRingEntry(t2.bytes, t2.len, RING_STATUS_READY, "user_msg");
    TEST_ASSERT_TRUE(txRingBeginTx(s2, &prev));
    txRingFinishTx(s2, prev, TXRING_FIN_DROP);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[s2][0]);

    // Ein waehrenddessen per ACK aufgeloester Slot (nicht mehr INFLIGHT)
    // bleibt, wie er ist: Finish fasst ihn nicht an.
    BuiltFrame t3 = buildTextFrame("DK5EN-91", "acked", 0xC804UL);
    int s3 = addTxRingEntry(t3.bytes, t3.len, RING_STATUS_READY, "user_msg");
    TEST_ASSERT_TRUE(txRingBeginTx(s3, &prev));
    ringBuffer[s3][1] = RING_STATUS_DONE;   // ACK-Scan loest auf ...
    ringBuffer[s3][0] = 0;                  // ... und gibt frei
    txRingFinishTx(s3, prev, TXRING_FIN_SENT);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[s3][0]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, ringBuffer[s3][1]);
}

// EXT_PENDING-Slot wird nie verdraengt (auch ausserhalb von EXTERNAL_RADIO-Builds
// schuetzt der Pool den Status).
static void test_pool_ext_pending_wird_nie_verdraengt(void)
{
    BuiltFrame pos = buildPositionFrame(0xC900UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "ext"));
    for (int i = 1; i < MAX_RING; i++)
    {
        BuiltFrame f = buildAckFrame(0xC910UL + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "ext"));
    }
    ringBuffer[0][1] = RING_STATUS_EXT_PENDING;

    BuiltFrame ack = buildAckFrame(0xC9FFUL);
    TEST_ASSERT_EQUAL_INT(RING_ENQUEUE_FULL, addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "ext"));
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_EXT_PENDING, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)pos.len, ringBuffer[0][0]);
}

// Ein geparkter SENT-Slot (eigener Text, wartet auf ACK) IST verdraengbar --
// wie im alten Ring --, aber nur von einer strikt hoeheren Prioritaet.
static void test_pool_geparkter_sent_slot_ist_verdraengbar_nur_von_hoeherer_prio(void)
{
    // Pool voll mit Gruppentexten (HIGH, Prio 2), Slot 0 aelter und geparkt.
    mc_test_set_millis(1);
    for (int i = 0; i < MAX_RING; i++)
    {
        BuiltFrame f = buildTextFrame("9", "grp", 0xCA00UL + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "user_msg"));
        mc_test_set_millis(2 + (uint32_t)i);
    }
    ringBuffer[0][1] = 0x07;

    // Gleiche Prio (2): verworfen, Parker bleibt.
    BuiltFrame g = buildTextFrame("9", "grp2", 0xCAF0UL);
    TEST_ASSERT_EQUAL_INT(RING_ENQUEUE_FULL, addTxRingEntry(g.bytes, g.len, RING_STATUS_READY, "user_msg"));
    TEST_ASSERT_EQUAL_UINT8(0x07, ringBuffer[0][1]);

    // Prio 1 (DM): verdraengt den aeltesten der schlechtesten Prio = den Parker.
    BuiltFrame dm = buildTextFrame("DK5EN-91", "dm", 0xCAF1UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(dm.bytes, dm.len, RING_STATUS_READY, "user_msg"));
    // Buchung: 1 verworfener Neuer (gleiche Prio) + 1 verdraengter Parker.
    TEST_ASSERT_EQUAL_UINT16(2, stat_drop_count[MSG_PRIO_HIGH]);
}

// stat_preempt_count-Definition: gewaehlter Slot ist nicht der aelteste belegte.
static void test_pool_is_preempt_aeltester_belegter_slot(void)
{
    mc_test_set_millis(10);
    BuiltFrame a = buildHeyFrame(0xCB01UL);      // aelter, BACKGROUND
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(a.bytes, a.len, RING_STATUS_READY, "pre"));
    mc_test_set_millis(20);
    BuiltFrame b = buildAckFrame(0xCB02UL);      // juenger, CRITICAL -> wird gewaehlt
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(b.bytes, b.len, RING_STATUS_READY, "pre"));

    TEST_ASSERT_EQUAL_INT(1, getNextTxSlot());
    TEST_ASSERT_TRUE(txRingIsPreempt(1));    // Slot 0 ist aelter
    TEST_ASSERT_FALSE(txRingIsPreempt(0));   // der aelteste selbst: keine Ueberholung

    ringBuffer[0][0] = 0;
    TEST_ASSERT_FALSE(txRingIsPreempt(1));   // allein
}

// txRingDepth()/txRingPrioCounts() zaehlen jeden belegten Slot (INFLIGHT, SENT,
// EXT_PENDING eingeschlossen); txRingAckWaitCounts() nur die SENT-Leiter.
static void test_pool_depth_und_ackwait_counts(void)
{
    BuiltFrame dm   = buildTextFrame("DK5EN-91", "dm", 0xCC01UL);   // Prio 1
    BuiltFrame grp  = buildTextFrame("9", "grp", 0xCC02UL);         // Prio 2
    BuiltFrame grp2 = buildTextFrame("9", "grp2", 0xCC03UL);        // Prio 2
    BuiltFrame rdy  = buildTextFrame("9", "rdy", 0xCC04UL);         // Prio 2, READY bleibt
    BuiltFrame pos  = buildPositionFrame(0xCC05UL);                 // Prio 4
    BuiltFrame hey  = buildHeyFrame(0xCC06UL);                      // Prio 5
    int sDm  = addTxRingEntry(dm.bytes,  dm.len,  RING_STATUS_READY, "user_msg");
    int sGrp = addTxRingEntry(grp.bytes, grp.len, RING_STATUS_READY, "user_msg");
    int sGr2 = addTxRingEntry(grp2.bytes, grp2.len, RING_STATUS_READY, "user_msg");
    int sRdy = addTxRingEntry(rdy.bytes, rdy.len, RING_STATUS_READY, "user_msg");
    int sPos = addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "user_pos");
    int sHey = addTxRingEntry(hey.bytes, hey.len, RING_STATUS_READY, "beacon");
    (void)sRdy;

    ringBuffer[sDm][1]  = 0x01;                      // SENT-Leiter, Beginn
    ringBuffer[sGrp][1] = 0x14;                      // SENT-Leiter, Ende
    ringBuffer[sGr2][1] = 0x15;                      // transienter Schwellwert, zaehlt noch
    ringBuffer[sPos][1] = RING_STATUS_INFLIGHT;      // gerade auf Sendung
    ringBuffer[sHey][1] = RING_STATUS_EXT_PENDING;   // externes Funkmodul

    uint8_t all[6], wait[6];
    txRingPrioCounts(all);
    txRingAckWaitCounts(wait);

    TEST_ASSERT_EQUAL_INT(6, txRingDepth());
    TEST_ASSERT_EQUAL_UINT8(6, all[0]);
    TEST_ASSERT_EQUAL_UINT8(1, all[MSG_PRIO_CRITICAL]);
    TEST_ASSERT_EQUAL_UINT8(3, all[MSG_PRIO_HIGH]);
    TEST_ASSERT_EQUAL_UINT8(1, all[MSG_PRIO_LOW]);
    TEST_ASSERT_EQUAL_UINT8(1, all[MSG_PRIO_BACKGROUND]);

    TEST_ASSERT_EQUAL_UINT8(3, wait[0]);
    TEST_ASSERT_EQUAL_UINT8(1, wait[MSG_PRIO_CRITICAL]);
    TEST_ASSERT_EQUAL_UINT8(2, wait[MSG_PRIO_HIGH]);
    TEST_ASSERT_EQUAL_UINT8(0, wait[MSG_PRIO_NORMAL]);
    TEST_ASSERT_EQUAL_UINT8(0, wait[MSG_PRIO_LOW]);
    TEST_ASSERT_EQUAL_UINT8(0, wait[MSG_PRIO_BACKGROUND]);

    // Freie Slots (len 0) mit altem SENT-Status zaehlen nicht.
    ringBuffer[sDm][0] = 0;
    txRingAckWaitCounts(wait);
    TEST_ASSERT_EQUAL_UINT8(2, wait[0]);
    TEST_ASSERT_EQUAL_UINT8(0, wait[MSG_PRIO_CRITICAL]);
}

// Der Alterungs-Sweep laesst einen INFLIGHT-Slot in Ruhe (doTX() gibt ihn selbst frei).
static void test_pool_sweep_ueberspringt_inflight(void)
{
    mc_test_set_millis(1000);
    BuiltFrame f = buildHeyFrame(0xCD01UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "sweep"));
    ringBuffer[0][1] = RING_STATUS_INFLIGHT;

    mc_test_set_millis(1000UL + RING_BG_MAX_AGE_MS + 1UL);
    txRingAgeBackground((uint32_t)millis(), kNoClock);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[0][0]);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_BACKGROUND]);
}

// W1b-Advisor R1: liegt nur ein geparkter SENT-Text im Pool, gibt es nichts zu
// senden -- die TX-Gates der Hauptschleifen (esp32_main/nrf52_main) fragen
// darum getNextTxSlot() >= 0, nicht txRingDepth() > 0.
static void test_pool_nur_geparkter_sent_nichts_zu_senden(void)
{
    BuiltFrame txt = buildTextFrame("DK5EN-91", "warte auf ack", 0xC700UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(txt.bytes, txt.len, RING_STATUS_READY, "user_msg"));
    uint8_t prev = 0xEE;
    TEST_ASSERT_TRUE(txRingBeginTx(0, &prev));
    txRingFinishTx(0, prev, TXRING_FIN_SENT);
    TEST_ASSERT_EQUAL_INT(1, txRingDepth());
    TEST_ASSERT_EQUAL_INT(-1, getNextTxSlot());
}

// W1b-Advisor R2: zwischen getNextTxSlot() (ohne Lock) und dem Claim kann
// OnRxDone (nRF52, LORA-Task) den Slot verdraengen und neu belegen. Der Claim
// prueft darum unter dem Lock die Einreihzeit des gewaehlten Eintrags.
static void test_pool_claim_scheitert_an_neu_belegtem_slot(void)
{
    mc_test_set_millis(1000);
    BuiltFrame a = buildTextFrame("DK5EN-92", "alt", 0xC710UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(a.bytes, a.len, RING_STATUS_DONE, "rx_relay"));
    int pick = getNextTxSlot();
    TEST_ASSERT_EQUAL_INT(0, pick);
    uint32_t snap = ringEnqueueTime[pick];

    // "OnRxDone": Slot frei und mit einem neuen Frame belegt
    ringBuffer[0][0] = 0;
    mc_test_set_millis(2000);
    BuiltFrame b = buildTextFrame("DK5EN-92", "neu", 0xC711UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(b.bytes, b.len, RING_STATUS_DONE, "rx_relay"));

    uint8_t prev = 0xEE;
    TEST_ASSERT_FALSE(txRingBeginTx(pick, &prev, &snap));
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_DONE, (uint8_t)ringBuffer[0][1]);   // der Neue bleibt unberuehrt

    // ohne Wechsel gelingt der Claim mit Schnappschuss
    snap = ringEnqueueTime[0];
    TEST_ASSERT_TRUE(txRingBeginTx(0, &prev, &snap));
}

// ------------------------------------------------- TXP W1c: supersede + rearm

static BuiltFrame heyFrom(uint32_t id, const char *src)
{
    static const uint8_t payload[] = "R0;";
    return buildFrame(MSG_TYPE_HEY, id, 0x92, src, "*", payload, sizeof(payload) - 1);
}

static BuiltFrame posFrom(uint32_t id, const char *src)
{
    static const uint8_t payload[] = "4825.35N\\01147.19E-Test";
    return buildFrame(MSG_TYPE_POSITION, id, 0x12, src, "*", payload, sizeof(payload) - 1);
}

static uint32_t slotMsgId(int slot)
{
    return ((uint32_t)ringBuffer[slot][6] << 24) | ((uint32_t)ringBuffer[slot][5] << 16) |
           ((uint32_t)ringBuffer[slot][4] << 8) | (uint32_t)ringBuffer[slot][3];
}

static void test_w1c_eigenes_hey_wird_in_place_ersetzt(void)
{
    BuiltFrame a = heyFrom(0xA0000001UL, "DK5EN-90");
    BuiltFrame b = heyFrom(0xA0000002UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(a.bytes, a.len, RING_STATUS_READY, "hey"));
    mc_test_set_millis(5000);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(b.bytes, b.len, RING_STATUS_READY, "hey"));
    TEST_ASSERT_EQUAL_INT(1, txRingDepth());
    TEST_ASSERT_EQUAL_HEX32(0xA0000002UL, slotMsgId(0));
    TEST_ASSERT_EQUAL_UINT32(5000, ringEnqueueTime[0]);
    TEST_ASSERT_EQUAL_UINT32(1, stat_supersede_count);
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[5]);
}

static void test_w1c_relay_hey_zwei_urheber_zwei_slots(void)
{
    BuiltFrame a = heyFrom(0xA0000011UL, "OE1AAA-1,DK5EN-90");
    BuiltFrame b = heyFrom(0xA0000012UL, "OE1BBB-1,DK5EN-90");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(a.bytes, a.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(b.bytes, b.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(2, txRingDepth());
    // derselbe Urheber ueber einen anderen Relay-Pfad ersetzt
    BuiltFrame c = heyFrom(0xA0000013UL, "OE1AAA-1,OE9ZZZ-1");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(c.bytes, c.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(2, txRingDepth());
    TEST_ASSERT_EQUAL_HEX32(0xA0000013UL, slotMsgId(0));
    TEST_ASSERT_EQUAL_HEX32(0xA0000012UL, slotMsgId(1));
}

static void test_w1c_position_gleicher_urheber_ersetzt_hey_nicht(void)
{
    BuiltFrame p1 = posFrom(0xB0000001UL, "DK5EN-90");
    BuiltFrame p2 = posFrom(0xB0000002UL, "DK5EN-90");
    BuiltFrame h  = heyFrom(0xB0000003UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(p1.bytes, p1.len, RING_STATUS_READY, "pos"));
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(h.bytes, h.len, RING_STATUS_READY, "hey"));   // andere Prio/Typ
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(p2.bytes, p2.len, RING_STATUS_READY, "pos"));
    TEST_ASSERT_EQUAL_INT(2, txRingDepth());
    TEST_ASSERT_EQUAL_HEX32(0xB0000002UL, slotMsgId(0));
    TEST_ASSERT_EQUAL_HEX32(0xB0000003UL, slotMsgId(1));
}

static void test_w1c_sent_und_inflight_werden_nie_ersetzt(void)
{
    BuiltFrame a = posFrom(0xC0000001UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(a.bytes, a.len, RING_STATUS_READY, "pos"));
    ringBuffer[0][1] = 0x05;   // SENT-Leiter
    BuiltFrame b = posFrom(0xC0000002UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(b.bytes, b.len, RING_STATUS_READY, "pos"));
    ringBuffer[1][1] = RING_STATUS_INFLIGHT;
    BuiltFrame c = posFrom(0xC0000003UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(2, addTxRingEntry(c.bytes, c.len, RING_STATUS_READY, "pos"));
    ringBuffer[2][1] = RING_STATUS_EXT_PENDING;
    BuiltFrame d = posFrom(0xC0000004UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(3, addTxRingEntry(d.bytes, d.len, RING_STATUS_READY, "pos"));
    TEST_ASSERT_EQUAL_INT(4, txRingDepth());
    TEST_ASSERT_EQUAL_HEX32(0xC0000001UL, slotMsgId(0));
    TEST_ASSERT_EQUAL_UINT32(0, stat_supersede_count);
}

static void test_w1c_text_dm_ack_werden_nie_ersetzt(void)
{
    BuiltFrame t1 = buildTextFrame("*", "hallo", 0xD0000001UL);
    BuiltFrame t2 = buildTextFrame("*", "hallo", 0xD0000002UL);
    BuiltFrame d1 = buildTextFrame("DK5EN-92", "dm", 0xD0000003UL);
    BuiltFrame d2 = buildTextFrame("DK5EN-92", "dm", 0xD0000004UL);
    BuiltFrame k1 = buildAckFrame(0xD0000005UL);
    BuiltFrame k2 = buildAckFrame(0xD0000006UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(t1.bytes, t1.len, RING_STATUS_READY, "t"));
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(t2.bytes, t2.len, RING_STATUS_READY, "t"));
    TEST_ASSERT_EQUAL_INT(2, addTxRingEntry(d1.bytes, d1.len, RING_STATUS_READY, "t"));
    TEST_ASSERT_EQUAL_INT(3, addTxRingEntry(d2.bytes, d2.len, RING_STATUS_READY, "t"));
    TEST_ASSERT_EQUAL_INT(4, addTxRingEntry(k1.bytes, k1.len, RING_STATUS_DONE, "a"));
    TEST_ASSERT_EQUAL_INT(5, addTxRingEntry(k2.bytes, k2.len, RING_STATUS_DONE, "a"));
    TEST_ASSERT_EQUAL_INT(6, txRingDepth());
    TEST_ASSERT_EQUAL_UINT32(0, stat_supersede_count);
}

static void test_w1c_cet_neuer_ersetzt_aelteren_anderer_quelle(void)
{
    BuiltFrame o = buildCetFrame(kCetNow - 60);
    BuiltFrame n = buildCetFrame(kCetNow);
    // andere Quelle: Rufzeichen im Pfad tauschen (gleiche Laenge)
    memcpy(n.bytes + 6, "OE2YYY-1", 8);
    n.bytes[1] = 0x99;
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(o.bytes, o.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(n.bytes, n.len, RING_STATUS_DONE, "udp_rx"));
    TEST_ASSERT_EQUAL_INT(1, txRingDepth());
    TEST_ASSERT_EQUAL_UINT8(0x99, ringBuffer[0][3]);
    TEST_ASSERT_EQUAL_UINT32(1, stat_supersede_count);
}

static void test_w1c_cet_aelterer_neuer_frame_wird_verworfen(void)
{
    BuiltFrame q = buildCetFrame(kCetNow);
    BuiltFrame o = buildCetFrame(kCetNow - 60);
    o.bytes[1] = 0x77;
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(q.bytes, q.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(-1, addTxRingEntry(o.bytes, o.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(1, txRingDepth());
    TEST_ASSERT_EQUAL_UINT8(0x01, ringBuffer[0][3]);   // der Gleiche bleibt (id 0xCE700001)
    TEST_ASSERT_EQUAL_UINT32(0, stat_supersede_count);
}

static void test_w1c_cet_ersetzt_keinen_anderen_text(void)
{
    BuiltFrame t = buildTextFrame("*", "kein cet", 0xE0000001UL, "OE1XXX-1");
    BuiltFrame n = buildCetFrame(kCetNow);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(t.bytes, t.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(n.bytes, n.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_INT(2, txRingDepth());
}

static void test_w1c_voller_pool_hey_wird_ersetzt_statt_verworfen(void)
{
    BuiltFrame h = heyFrom(0xF0000001UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(h.bytes, h.len, RING_STATUS_READY, "hey"));
    for (int i = 1; i < MAX_RING; i++)
    {
        BuiltFrame f = buildTextFrame("DK5EN-92", "x", 0xF1000000UL + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "fill"));
    }
    BuiltFrame n = heyFrom(0xF0000002UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(n.bytes, n.len, RING_STATUS_READY, "hey"));
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());
    TEST_ASSERT_EQUAL_HEX32(0xF0000002UL, slotMsgId(0));
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[5]);
}

static void test_w1c_rearm_im_vollen_pool(void)
{
    BuiltFrame p = buildTextFrame("DK5EN-92", "geparkt", 0x71000000UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(p.bytes, p.len, RING_STATUS_READY, "msg"));
    ringBuffer[0][1] = 0x15;
    ringSource[0] = 'm';
    for (int i = 1; i < MAX_RING; i++)
    {
        BuiltFrame f = buildTextFrame("DK5EN-92", "fill", 0x72000000UL + (uint32_t)i);
        TEST_ASSERT_EQUAL_INT(i, addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "fill"));
    }
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());

    BuiltFrame r = p;
    r.bytes[1] = 0x42; r.bytes[2] = 0x43; r.bytes[3] = 0x44; r.bytes[4] = 0x45;   // neue msg-id 0x45444342
    mc_test_set_millis(9000);
    TEST_ASSERT_TRUE(txRingRearmRetry(0, r.bytes, r.len, 0x15));
    TEST_ASSERT_EQUAL_INT(MAX_RING, txRingDepth());
    TEST_ASSERT_EQUAL_HEX32(0x45444342UL, slotMsgId(0));
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_READY, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8(1, retryCount[0]);
    TEST_ASSERT_EQUAL_UINT32(9000, ringEnqueueTime[0]);
    TEST_ASSERT_EQUAL_UINT8('m', ringSource[0]);
    TEST_ASSERT_EQUAL_UINT8(1, ringPriority[0]);
    for (int i = 1; i < MAX_RING; i++)
        TEST_ASSERT_EQUAL_HEX32(0x72000000UL + (uint32_t)i, slotMsgId(i));
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[1]);

    // 1:1-Wiederholung: frame zeigt auf den Slot selbst
    ringBuffer[0][1] = 0x15;
    TEST_ASSERT_TRUE(txRingRearmRetry(0, &ringBuffer[0][2], ringBuffer[0][0], 0x15));
    TEST_ASSERT_EQUAL_UINT8(2, retryCount[0]);
    TEST_ASSERT_EQUAL_HEX32(0x45444342UL, slotMsgId(0));
}

static void test_w1c_rearm_verliert_gegen_ack_oder_laenge(void)
{
    BuiltFrame p = buildTextFrame("DK5EN-92", "geparkt", 0x71000000UL);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(p.bytes, p.len, RING_STATUS_READY, "msg"));
    ringBuffer[0][1] = 0x15;
    BuiltFrame r = p;
    r.bytes[1] = 0x42;
    TEST_ASSERT_FALSE(txRingRearmRetry(0, r.bytes, r.len, 0x14));        // Status passt nicht
    TEST_ASSERT_FALSE(txRingRearmRetry(0, r.bytes, (uint16_t)(r.len - 1), 0x15));   // Laenge passt nicht
    TEST_ASSERT_EQUAL_HEX32(0x71000000UL, slotMsgId(0));
    TEST_ASSERT_EQUAL_UINT8(0x15, ringBuffer[0][1]);
    TEST_ASSERT_EQUAL_UINT8(0, retryCount[0]);
    ringBuffer[0][1] = RING_STATUS_DONE;                                  // ACK gewann
    TEST_ASSERT_FALSE(txRingRearmRetry(0, r.bytes, r.len, 0x15));
    ringBuffer[0][0] = 0;                                                 // Slot frei
    TEST_ASSERT_FALSE(txRingRearmRetry(0, r.bytes, r.len, 0x15));
    TEST_ASSERT_FALSE(txRingRearmRetry(-1, r.bytes, r.len, 0x15));
    TEST_ASSERT_FALSE(txRingRearmRetry(MAX_RING, r.bytes, r.len, 0x15));
}

// W1c-Advisor 1: eigenes HEY ("H") und eigener NBR-Bericht ("HN") sind beide
// '@' mit prio 5 vom selben Urheber -- verschiedene Ziele, kein Supersede.
static void test_w1c_hey_und_hn_bericht_verschiedene_ziele(void)
{
    static const uint8_t pl[] = "R0;";
    BuiltFrame h  = buildFrame(MSG_TYPE_HEY, 0xA1000001UL, 0x92, "DK5EN-90", "H", pl, sizeof(pl) - 1);
    BuiltFrame hn = buildFrame(MSG_TYPE_HEY, 0xA1000002UL, 0x90, "DK5EN-90", "HN", pl, sizeof(pl) - 1);
    BuiltFrame h2 = buildFrame(MSG_TYPE_HEY, 0xA1000003UL, 0x92, "DK5EN-90", "H", pl, sizeof(pl) - 1);
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(h.bytes, h.len, RING_STATUS_READY, "hey"));
    TEST_ASSERT_EQUAL_INT(1, addTxRingEntry(hn.bytes, hn.len, RING_STATUS_READY, "hn"));
    TEST_ASSERT_EQUAL_INT(2, txRingDepth());
    TEST_ASSERT_EQUAL_UINT32(0, stat_supersede_count);
    // ein neueres "H" ersetzt nur das alte "H"
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(h2.bytes, h2.len, RING_STATUS_READY, "hey"));
    TEST_ASSERT_EQUAL_HEX32(0xA1000002UL, slotMsgId(1));
    TEST_ASSERT_EQUAL_UINT32(1, stat_supersede_count);
}

// W1c-Advisor 2: ein vom Nutzer getippter Text, der mit "{CET}" beginnt, ist
// kein Server-Beacon: weder ersetzt er einen gerelayten CET noch umgekehrt.
static void test_w1c_nutzertext_mit_cet_praefix_bleibt_text(void)
{
    BuiltFrame relay = buildTextFrame("*", "{CET}2026-10-24 12:00:10", 0xA2000001UL, "OE1XAR-33,DK5EN-98");
    BuiltFrame user  = buildTextFrame("*", "{CET}2026-10-24 12:00:00", 0xA2000002UL, "DK5EN-90");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(relay.bytes, relay.len, RING_STATUS_DONE, "rx_relay"));
    int u = addTxRingEntry(user.bytes, user.len, RING_STATUS_READY, "user_msg");
    TEST_ASSERT_EQUAL_INT(1, u);
    TEST_ASSERT_EQUAL_INT(2, txRingDepth());
    // und ein neuerer gerelayter CET ersetzt den Nutzertext nicht
    BuiltFrame relay2 = buildTextFrame("*", "{CET}2026-10-24 12:05:10", 0xA2000003UL, "OE1XAR-62,DK5EN-98");
    TEST_ASSERT_EQUAL_INT(0, addTxRingEntry(relay2.bytes, relay2.len, RING_STATUS_DONE, "rx_relay"));
    TEST_ASSERT_EQUAL_HEX32(0xA2000002UL, slotMsgId(1));
    TEST_ASSERT_EQUAL_UINT32(1, stat_supersede_count);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_einfacher_enqueue);
    RUN_TEST(test_prioritaets_klassifizierung);
    RUN_TEST(test_add_tx_ring_entry_once_dm_wird_critical_und_done);
    RUN_TEST(test_add_tx_ring_entry_once_gruppe_wird_high_und_done);
    RUN_TEST(test_add_tx_ring_entry_once_broadcast_wird_high_und_done);
    RUN_TEST(test_add_tx_ring_entry_mit_done_klassifiziert_weiter_als_relay);
    RUN_TEST(test_add_tx_ring_entry_once_slot_ist_fuer_getnexttxslot_done);
    RUN_TEST(test_rm1_dm_once_wird_critical_und_done);
    RUN_TEST(test_rm1_dm_mit_status_done_vorab_ist_relay_klasse);
    RUN_TEST(test_rm1_command_status0_wird_critical_und_nicht_done);
    RUN_TEST(test_add_tx_ring_entry_once_verdraengt_relay_statt_verworfen_zu_werden);
    RUN_TEST(test_ring_wrap);
    RUN_TEST(test_overflow_mit_eviction);
    RUN_TEST(test_eviction_trifft_aeltesten_der_schlechtesten_prio_und_nichts_wandert);
    RUN_TEST(test_sl06_ringsource_aus_label_und_bei_verdraengung_kopiert);
    RUN_TEST(test_stufe2_default_ueberschreibt_alte_maske);
    RUN_TEST(test_stufe2_kind_need_alone_werden_gesetzt);
    RUN_TEST(test_gwrx01_server_text_mit_bekanntem_bedarf_wird_relay);
    RUN_TEST(test_gwrx01_server_nichttext_bleibt_other);
    RUN_TEST(test_gwrx01_ohne_nbrrelay_oder_unbekannt_wie_heute);
    RUN_TEST(test_stufe2_eviction_ueberschreibt_maske_im_uebernommenen_slot);
    RUN_TEST(test_stufe2_maskenbit_ueber_32_ueberlebt_case_b);
    RUN_TEST(test_qrt_f5_voll_ohne_eviction_liefert_minus2);
    RUN_TEST(test_qrt_f5_laenge_null_bleibt_minus1);
    RUN_TEST(test_len_null_wird_abgewiesen);
    RUN_TEST(test_len_ueber_max_wird_abgewiesen);
    RUN_TEST(test_len_exakt_max_wird_enqueued);
    RUN_TEST(test_overflow_drop);
    RUN_TEST(test_get_next_tx_slot);
    RUN_TEST(test_retry_count_in);
    RUN_TEST(test_clear_slot_first);
    RUN_TEST(test_n14_zwei_aufrufe_verschiedene_slots);
    RUN_TEST(test_n14_stress_randomisiert);
    RUN_TEST(test_bp02_tiefe_mit_loechern);
    RUN_TEST(test_bp02_dj8meh_szenario_prio5_am_lesezeiger);
    RUN_TEST(test_bp03_stale_drop_und_slot_frei);
    RUN_TEST(test_bp03_frischer_bg_bleibt);
    RUN_TEST(test_bp03_nicht_bg_altert_nie);
    RUN_TEST(test_bp03_rollover_sicher);
    RUN_TEST(test_ttl_table);
    RUN_TEST(test_ttl_relay_300s);
    RUN_TEST(test_ttl_pos_120s);
    RUN_TEST(test_ttl_ack_90s);
    RUN_TEST(test_ttl_prio5_180s);
    RUN_TEST(test_ttl_own_text_never_ages);
    RUN_TEST(test_ttl_prio2_done_never_ages);
    RUN_TEST(test_ttl_own_once_dm_never_ages);
    RUN_TEST(test_ttl_own_once_group_never_ages);
    RUN_TEST(test_ttl_own_text_ack_ages_90s);
    RUN_TEST(test_ttl_ack_detection);
#if defined(EXTERNAL_RADIO)
    RUN_TEST(test_ttl_ext_pending_never_ages);
#endif
    RUN_TEST(test_cet_fresh_kept);
    RUN_TEST(test_cet_150s_kept);
    RUN_TEST(test_cet_151s_dropped);
    RUN_TEST(test_cet_untrusted_dropped);
    RUN_TEST(test_cet_no_now_dropped);
    RUN_TEST(test_cet_future_dropped);
    RUN_TEST(test_cet_drop_only_sweep);
    RUN_TEST(test_cet_non_cet_text_untrusted_kept);
    RUN_TEST(test_wq01_leerer_ring_liefert_nur_nullen);
    RUN_TEST(test_wq01_gemischte_prioritaeten_stimmen_mit_klassifizierung_ueberein);
    RUN_TEST(test_wq01_loch_in_der_mitte_wird_nicht_mitgezaehlt);
    RUN_TEST(test_nbr_caseb_hold_weicht_fall_a_gleicher_prio);
    RUN_TEST(test_nbr_caseb_hold_blockiert_hn_meldung_nicht);
    RUN_TEST(test_nbr_caseb_backoff_dreiphasig);
    RUN_TEST(test_nbr_caseb_kein_re_arm);
    RUN_TEST(test_nbr_ohne_bnbrcancel_unveraendert);
    RUN_TEST(test_nbr_caseb_text_relay_kein_hold);
    RUN_TEST(test_ringstat_parked_overwrite_bei_pending_slot);
    RUN_TEST(test_ringstat_parked_overwrite_bei_leerem_slot_bleibt_null);
    RUN_TEST(test_ringstat_parked_overwrite_ignoriert_done_ready_ext);
    RUN_TEST(test_pool_loch_hinter_geparktem_sent_slot_wird_gefuellt);
    RUN_TEST(test_pool_kapazitaet_ist_max_ring);
    RUN_TEST(test_pool_nur_geparkter_sent_nichts_zu_senden);
    RUN_TEST(test_pool_claim_scheitert_an_neu_belegtem_slot);
    RUN_TEST(test_pool_gleiche_prio_aelterer_eintrag_zuerst_auch_bei_hoeherem_index);
    RUN_TEST(test_pool_inflight_slot_ist_geschuetzt_und_rollback_stellt_status_her);
    RUN_TEST(test_pool_finish_tx_ausgaenge);
    RUN_TEST(test_pool_ext_pending_wird_nie_verdraengt);
    RUN_TEST(test_pool_geparkter_sent_slot_ist_verdraengbar_nur_von_hoeherer_prio);
    RUN_TEST(test_pool_is_preempt_aeltester_belegter_slot);
    RUN_TEST(test_pool_depth_und_ackwait_counts);
    RUN_TEST(test_pool_sweep_ueberspringt_inflight);
    RUN_TEST(test_w1c_eigenes_hey_wird_in_place_ersetzt);
    RUN_TEST(test_w1c_hey_und_hn_bericht_verschiedene_ziele);
    RUN_TEST(test_w1c_nutzertext_mit_cet_praefix_bleibt_text);
    RUN_TEST(test_w1c_relay_hey_zwei_urheber_zwei_slots);
    RUN_TEST(test_w1c_position_gleicher_urheber_ersetzt_hey_nicht);
    RUN_TEST(test_w1c_sent_und_inflight_werden_nie_ersetzt);
    RUN_TEST(test_w1c_text_dm_ack_werden_nie_ersetzt);
    RUN_TEST(test_w1c_cet_neuer_ersetzt_aelteren_anderer_quelle);
    RUN_TEST(test_w1c_cet_aelterer_neuer_frame_wird_verworfen);
    RUN_TEST(test_w1c_cet_ersetzt_keinen_anderen_text);
    RUN_TEST(test_w1c_voller_pool_hey_wird_ersetzt_statt_verworfen);
    RUN_TEST(test_w1c_rearm_im_vollen_pool);
    RUN_TEST(test_w1c_rearm_verliert_gegen_ack_oder_laenge);
    return UNITY_END();
}
