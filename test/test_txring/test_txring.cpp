// Native Testsuite fuer den TX-Ring-Kern (getMessagePriority, getNextTxSlot,
// advanceIReadPastEmpty, addTxRingEntry) -- nach txring_functions.cpp
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
#include <aprs_functions.h>
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
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)iWrite);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iRead);
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

// ----------------------------------------------------- Test 3: Ring-Wrap

static void test_ring_wrap(void)
{
    // Phase 1: MAX_RING-1 sequentielle Enqueues fuellen den Ring bis knapp
    // unter "voll" (der letzte Slot bleibt frei -- klassisches Ringpuffer-
    // Vollkriterium ueber nextWrite==iRead).
    for (int i = 0; i < MAX_RING - 1; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x1000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t3");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_RING - 1, (uint8_t)iWrite);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iRead);

    // Alle Slots wieder freigeben (simuliert vollstaendige Konsumierung durch
    // doTX) -- der Ring hat danach keine Daten mehr, iRead holt zu iWrite auf.
    for (int i = 0; i < MAX_RING - 1; i++)
        ringBuffer[i][0] = 0;
    advanceIReadPastEmpty();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)iWrite, (uint8_t)iRead);

    // Phase 2 "dann wrap": drei weitere Enqueues muessen bei MAX_RING-1
    // beginnen und ueber den Indexrand auf 0, 1 wickeln.
    int expected[3] = {MAX_RING - 1, 0, 1};
    for (int i = 0; i < 3; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x2000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t3wrap");
        TEST_ASSERT_EQUAL_INT_MESSAGE(expected[i], slot, "wrap-slot");
    }
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)iWrite); // letzter belegter Slot war 1 -> iWrite==2
}

// ----------------------------------------------------- Test 4: Overflow mit Eviction

static void test_overflow_mit_eviction(void)
{
    // Ring mit MAX_RING-1 LOW-Prio-Eintraegen (Position) "voll" fuellen.
    for (int i = 0; i < MAX_RING - 1; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x3000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t4fill");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_RING - 1, (uint8_t)iWrite);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iRead);

    // Eine CRITICAL-DM enqueuen -> muss den aeltesten (= Slot 0, da alle
    // gleiche Prio LOW haben und der Scan bei iRead beginnt) LOW-Eintrag
    // verdraengen.
    BuiltFrame dm = buildTextFrame("DK5EN-91", "dringend");
    int slot = addTxRingEntry(dm.bytes, dm.len, RING_STATUS_READY, "t4evict");

    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, slot); // landet im zuvor freien letzten Slot
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[slot]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(dm.bytes, &ringBuffer[slot][2], dm.len);

    // Slot 0 (aeltester LOW-Eintrag) wurde als leer markiert (Wiederverwendungs-
    // Semantik: [0]==0 vor der naechsten Wiederbeschreibung).
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]);

    // iRead ist ueber den entleerten Slot 0 hinweg auf 1 vorgerueckt.
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)iRead);

    // iWrite ist auf 0 gewickelt (MAX_RING-1 + 1 == MAX_RING -> 0).
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iWrite);

    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
}

// ----------------------------------------------- Fund: verwaister Slot bei
// indirekter Eviction (TXRING-ORPHAN-SLOT)
//
// Waehrend des Schreibens dieser Suite entdeckt (siehe Wave-Report): die
// Overflow-Logik in addTxRingEntry() hat zwei voneinander unabhaengige
// "Ring voll"-Pfade, die sich gegenseitig nicht abstimmen:
//
//   1) Die Prio-Eviction weiter oben waehlt den Slot mit der SCHLECHTESTEN
//      Prioritaet im gesamten Fenster [iRead,iWrite) zur Verdraengung -- das
//      muss NICHT der Slot an iRead sein, wenn irgendwo dazwischen ein noch
//      niedriger priorisierter Eintrag liegt.
//   2) Der anschliessende unbedingte Block "ring full: advance read pointer
//      past the overwritten slot" prueft NUR noch, ob iWrite nach dem
//      Schreiben rechnerisch mit iRead kollidiert -- unabhaengig davon,
//      WELCHER Slot in Schritt 1 tatsaechlich entleert wurde.
//
// Wird in Schritt 1 ein Slot verdraengt, der NICHT der an iRead ist, bleibt
// der Slot an iRead unveraendert belegt (advanceIReadPastEmpty() haelt sofort
// an, weil [iRead][0] noch >0 ist) -- aber Schritt 2 schiebt iRead trotzdem
// eins weiter, weil die Kollisionsrechnung nur auf dem urspruenglichen iRead
// beruht. Der Slot an der alten iRead-Position bleibt mit [0]>0 im Speicher
// stehen, faellt aber aus dem von getNextTxSlot() gescannten Fenster
// [iRead,iWrite) heraus: eine gueltige, nie gesendete Nachricht wird still
// und ohne stat_drop_count-Buchung verworfen (erst ein spaeteres, volles
// Ring-Wrap ueberschreibt den Slot wieder physisch).
//
// GEFIXT (N-24): der Eviction-Pfad zieht den Eintrag von iRead in den
// soeben freigewordenen Slot um (Payload + Seitenarrays), leert iRead und
// laesst advanceIReadPastEmpty() regulaer weiterruecken -- kein belegter
// Slot faellt mehr aus dem Fenster, kein stiller Verlust. Preis: der
// umgezogene Eintrag verliert seinen FIFO-Rang innerhalb gleicher
// Prioritaet. Dieser Test fixiert das GEFIXTE Verhalten und schlaegt fehl,
// falls einer der beiden Ueberlauf-Pfade wieder auseinanderlaeuft.
static void test_n24_indirekte_eviction_verwaist_keinen_slot(void)
{
    // Slot 0: CRITICAL (beste Prio, bleibt bei der Eviction-Auswahl unangetastet)
    BuiltFrame ack = buildAckFrame(0xC0FFEEUL);
    int slot0 = addTxRingEntry(ack.bytes, ack.len, RING_STATUS_READY, "orphan0");
    TEST_ASSERT_EQUAL_INT(0, slot0);

    // Slots 1..4: LOW (Position) -- Fuellmaterial
    for (int i = 1; i <= 4; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x5000 + i));
        addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "orphanfill");
    }

    // Slot 5: BACKGROUND (HEY) -- schlechteste Prio im Fenster, wird bei der
    // naechsten Eviction gewaehlt, NICHT Slot 0.
    BuiltFrame hey = buildHeyFrame(0x6000UL);
    int slot5 = addTxRingEntry(hey.bytes, hey.len, RING_STATUS_READY, "orphanworst");
    TEST_ASSERT_EQUAL_INT(5, slot5);

    // Slots 6..18: weiteres LOW-Fuellmaterial, Ring damit bis MAX_RING-1 voll.
    for (int i = 6; i < MAX_RING - 1; i++)
    {
        BuiltFrame f = buildPositionFrame((uint32_t)(0x7000 + i));
        addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "orphanfill2");
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_RING - 1, (uint8_t)iWrite);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iRead);

    // Ein weiterer LOW-Eintrag ist besser als das BACKGROUND-Fuellstueck in
    // Slot 5 (4 < 5) -> Slot 5 wird verdraengt, nicht Slot 0.
    BuiltFrame trigger = buildPositionFrame(0x8000UL);
    int slotNew = addTxRingEntry(trigger.bytes, trigger.len, RING_STATUS_READY, "orphantrigger");
    TEST_ASSERT_EQUAL_INT(MAX_RING - 1, slotNew);

    // Der Drop traf ausschliesslich das BACKGROUND-Stueck (gebucht) --
    // und der CRITICAL-Eintrag von Slot 0 ist nach Slot 5 UMGEZOGEN.
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_BACKGROUND]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ack.len, ringBuffer[5][0]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ack.bytes, &ringBuffer[5][2], ack.len);
    TEST_ASSERT_EQUAL_UINT8(MSG_PRIO_CRITICAL, ringPriority[5]);

    // Slot 0 ist geleert, iRead regulaer auf den ersten belegten Slot (1)
    // weitergerueckt, iWrite steht auf 0 -- kein belegter Slot liegt mehr
    // ausserhalb des Fensters [iRead, iWrite).
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]);
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)iRead);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iWrite);

    // getNextTxSlot() findet den umgezogenen CRITICAL-Eintrag (Slot 5) --
    // die nie gesendete Nachricht bleibt sichtbar und gewinnt die Auswahl.
    TEST_ASSERT_EQUAL_INT(5, getNextTxSlot());

    // Kein unbilanzierter Verlust: CRITICAL wurde nie als Drop gebucht.
    TEST_ASSERT_EQUAL_UINT16(0, stat_drop_count[MSG_PRIO_CRITICAL]);
}

// ----------------------------------------------------- Test 5: Overflow-Drop

static void test_overflow_drop(void)
{
    // Ring mit MAX_RING-1 CRITICAL-Eintraegen (ACK) "voll" fuellen.
    for (int i = 0; i < MAX_RING - 1; i++)
    {
        BuiltFrame f = buildAckFrame((uint32_t)(0x4000 + i));
        int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t5fill");
        TEST_ASSERT_EQUAL_INT(i, slot);
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_RING - 1, (uint8_t)iWrite);
    uint8_t iReadBefore = (uint8_t)iRead;
    uint8_t iWriteBefore = (uint8_t)iWrite;

    // LOW-Prio-Position enqueuen: nichts im Ring hat niedrigere Prio als CRITICAL
    // -> der NEUE Eintrag wird verworfen, nicht der Bestand.
    BuiltFrame pos = buildPositionFrame(0x9999UL);
    int slot = addTxRingEntry(pos.bytes, pos.len, RING_STATUS_READY, "t5drop");

    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT8(iWriteBefore, (uint8_t)iWrite); // unveraendert
    TEST_ASSERT_EQUAL_UINT8(iReadBefore, (uint8_t)iRead);   // unveraendert
    TEST_ASSERT_EQUAL_UINT16(1, stat_drop_count[MSG_PRIO_LOW]);
    // Der Slot, in den der verworfene Eintrag geschrieben worden waere, ist
    // wieder auf [0]==0 zurueckgesetzt ("Don't enqueue").
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[MAX_RING - 1][0]);
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

    // Gleiche Prio: FIFO -- der Slot naeher an iRead gewinnt
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

    static const uint8_t payload[5] = {1, 2, 3, 4, 5};
    BuiltFrame f = buildFrame(MSG_TYPE_ACK, 0x0AUL, 0x00, nullptr, nullptr, payload, sizeof(payload));

    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t8", -1, true);
    TEST_ASSERT_EQUAL_INT(0, slot);

    TEST_ASSERT_EQUAL_UINT8((uint8_t)f.len, ringBuffer[slot][0]);
    TEST_ASSERT_EQUAL_UINT8(RING_STATUS_READY, ringBuffer[slot][1]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(f.bytes, &ringBuffer[slot][2], f.len);

    // Bytes zwischen Nutzlast-Ende und dem memset-Limit (Index UDP_TX_BUF_SIZE)
    // sind genullt.
    for (int i = 2 + f.len; i <= UDP_TX_BUF_SIZE; i++)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, ringBuffer[slot][i], "sollte genullt sein");

    // TXRING-CLEARSLOT-GAP: die letzten 4 Byte des Slots (Index
    // UDP_TX_BUF_SIZE+1 .. UDP_TX_BUF_SIZE+4) bleiben trotz clearSlotFirst=true
    // beim Alt-Muster 0xFF -- aktuelles (nicht gefixtes) Verhalten.
    for (int i = UDP_TX_BUF_SIZE + 1; i < UDP_TX_BUF_SIZE + 5; i++)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0xFF, ringBuffer[slot][i], "TXRING-CLEARSLOT-GAP");
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

    // Schatten-Buchhaltung MARKER-basiert statt Slot-basiert: der N-24-Fix
    // darf Eintraege bei indirekter Eviction zwischen Slots UMZIEHEN, eine
    // Slot-gebundene Schattenkopie wuerde danach falsch vergleichen. Der
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

        // Invariante: Indizes bleiben im gueltigen Bereich.
        TEST_ASSERT_TRUE((uint8_t)iWrite < MAX_RING);
        TEST_ASSERT_TRUE((uint8_t)iRead < MAX_RING);

        int w = (uint8_t)iWrite;
        int r = (uint8_t)iRead;

        for (int s = 0; s < MAX_RING; s++)
        {
            if (ringBuffer[s][0] == 0)
                continue; // leerer Slot -- nichts zu pruefen

            char ctx[64];
            snprintf(ctx, sizeof(ctx), "iter=%d slot=%d", iter, s);

            // Invariante (seit dem N-24-Fix wieder haltbar): jeder belegte
            // Slot liegt im aktiven Fenster [iRead, iWrite) -- kein Eintrag
            // faellt durch indirekte Eviction aus dem Scan-Bereich.
            bool inWindow = (w > r) ? (s >= r && s < w)
                                    : (w < r ? (s >= r || s < w) : false);
            TEST_ASSERT_TRUE_MESSAGE(inWindow, ctx);

            // Belegter Slot muss aus einem getrackten Enqueue stammen: der
            // Marker steht als msg_id-LSB an fester Position [3].
            uint8_t m = ringBuffer[s][3];
            TEST_ASSERT_TRUE_MESSAGE(markerKnown[m] != 0, ctx);

            // Kein "torn write": die gesamte Nutzlast-Region traegt exakt EIN
            // Muster -- auch nach einem N-24-Umzug des Eintrags.
            for (uint16_t i = 0; i < markerPayloadLen[m]; i++)
            {
                TEST_ASSERT_EQUAL_UINT8_MESSAGE(m, ringBuffer[s][markerOff[m] + i], ctx);
            }
        }
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_einfacher_enqueue);
    RUN_TEST(test_prioritaets_klassifizierung);
    RUN_TEST(test_ring_wrap);
    RUN_TEST(test_overflow_mit_eviction);
    RUN_TEST(test_n24_indirekte_eviction_verwaist_keinen_slot);
    RUN_TEST(test_overflow_drop);
    RUN_TEST(test_get_next_tx_slot);
    RUN_TEST(test_retry_count_in);
    RUN_TEST(test_clear_slot_first);
    RUN_TEST(test_n14_zwei_aufrufe_verschiedene_slots);
    RUN_TEST(test_n14_stress_randomisiert);
    return UNITY_END();
}
