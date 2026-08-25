// Layer-B-Replay: die Prioritaetsentscheidungen echter Knoten gegen den
// echten Klassifizierer nachfahren.
//
// Vorlage ist test/support/traces/txprio_trace.txt -- jedes Paar aus
// RING_WRITE und RING_PRIO, das laufende Knoten ausgegeben haben
// (tools/traceharvest.py, 25.045 Entscheidungen aus 48 Knotenstunden, auf die
// verschiedenen Faelle eingedampft).
//
// Warum das mehr ist als der bestehende test_txring: dort stehen
// handgebaute Frames, hier steht, was tatsaechlich in der Luft war -- fremde
// Firmware, echte Pfade, echte Zielrufzeichen. Besonders der Zweig, der bei
// Textnachrichten das ZIEL aus dem kodierten Pfad liest und daraus Broadcast
// (HIGH), Gruppe (HIGH) oder persoenliche DM (CRITICAL) macht, laesst sich
// nur mit echten Adressen sinnvoll pruefen.
//
// Der Ringslot wird byte-exakt neu aufgebaut: encodeAPRS() erzeugt aus den
// mitgeloggten Feldern denselben Frame, den der Knoten im Slot liegen hatte
// (dass das byte-genau stimmt, weist test_aprs_reencode gegen die
// Wire-Pruefsummen der Absender nach). Ringlayout laut txring_functions.cpp:
// [0]=len, [1]=status, [2..]=Frame.
//
//   pio test -e native_aprs -f test_txprio_replay

#include <unity.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <txring_functions.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
// Identischer Stub-Satz wie test_txring.cpp; BOARD_HARDWARE ist bewusst int,
// weil loop_functions_extern.h es so deklariert.
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- Helfer

#define HEXFIELD_MAX 1024
#define REPLAY_SLOT  0

static FILE *openRel(const char *rel, const char *mode)
{
    static const char *prefixes[] = { "", "../", "../../", "../../../", "../../../../" };
    char path[512];
    for (const char *p : prefixes)
    {
        snprintf(path, sizeof(path), "%s%s", p, rel);
        FILE *f = fopen(path, mode);
        if (f) return f;
    }
    return nullptr;
}

static String unhex(const char *hex)
{
    String s;
    if (hex[0] == '-' && hex[1] == 0) return s;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2)
    {
        int h = nib(hex[i]), l = nib(hex[i + 1]);
        if (h < 0 || l < 0) break;
        s.concat((char)((h << 4) | l));
    }
    return s;
}

// Baut den Ringslot so auf, wie ihn der Knoten hatte.
// Ohne Frameinhalt (src == "-") wird nur der Kopf gesetzt: fuer ACK, Position,
// HEY und weitergeleitete Texte liest getMessagePriority() nichts weiter als
// Typ und Status.
static void fillSlot(uint8_t type, uint8_t status, uint16_t len,
                     const String &src, const String &dst, const String &payload)
{
    memset(ringBuffer[REPLAY_SLOT], 0x00, sizeof(ringBuffer[REPLAY_SLOT]));
    ringBuffer[REPLAY_SLOT][0] = (uint8_t)len;
    ringBuffer[REPLAY_SLOT][1] = status;

    if (src.length() == 0)
    {
        ringBuffer[REPLAY_SLOT][2] = type;
        return;
    }

    struct aprsMessage m;
    initAPRS(m, (char)type);
    m.payload_type         = (char)type;
    m.msg_id               = 0x11223344;
    m.msg_source_path      = src;
    m.msg_destination_path = dst;
    m.msg_payload          = payload;

    uint8_t frame[UDP_TX_BUF_SIZE];
    memset(frame, 0x00, sizeof(frame));
    uint16_t flen = encodeAPRS(frame, m);
    if (flen > UDP_TX_BUF_SIZE + 2)
        flen = UDP_TX_BUF_SIZE + 2;

    for (uint16_t i = 0; i < flen && (size_t)(2 + i) < sizeof(ringBuffer[REPLAY_SLOT]); i++)
        ringBuffer[REPLAY_SLOT][2 + i] = frame[i];
}

// ---------------------------------------------------------------- Replay

static void test_txprio_trace_replay(void)
{
    FILE *f = openRel("test/support/traces/txprio_trace.txt", "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "txprio_trace.txt nicht gefunden (cwd?)");

    char line[4 * HEXFIELD_MAX];
    int checked = 0, bad = 0, shown = 0;
    int per_prio[8] = {0};

    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char name[16], srch[HEXFIELD_MAX], dsth[HEXFIELD_MAX], plh[HEXFIELD_MAX];
        unsigned type, status, len, prio;
        int n = sscanf(line, "%15s %x %x %u %u %1023s %1023s %1023s",
                       name, &type, &status, &len, &prio, srch, dsth, plh);
        if (n != 8) continue;

        fillSlot((uint8_t)type, (uint8_t)status, (uint16_t)len,
                 unhex(srch), unhex(dsth), unhex(plh));

        uint8_t got = getMessagePriority(REPLAY_SLOT);
        checked++;
        if (prio < 8) per_prio[prio]++;

        if (got != (uint8_t)prio)
        {
            bad++;
            if (shown++ < 10)
                printf("  %s type=%02X status=%02X: Feld sagte prio=%u, Code sagt %u  (dst=%s)\n",
                       name, type, status, prio, (unsigned)got, unhex(dsth).c_str());
        }
    }
    fclose(f);

    printf("\n[txprio] %d Entscheidungen geprueft, %d abweichend\n", checked, bad);
    printf("[txprio] je Prioritaet:");
    for (int i = 1; i <= 5; i++) printf("  %d:%d", i, per_prio[i]);
    printf("\n");

    TEST_ASSERT_GREATER_THAN_MESSAGE(100, checked,
        "zu wenige Entscheidungen -- Trace zu kurz oder Format geaendert");

    // Alle fuenf Prioritaetsklassen muessen vorkommen, sonst prueft der Lauf
    // nur einen Teil des Klassifizierers und meldet trotzdem "gruen".
    for (int i = 1; i <= 5; i++)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "Prioritaet %d kommt im Trace nicht vor", i);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, per_prio[i], msg);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad,
        "getMessagePriority() stuft anders ein als der Knoten im Feld");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_txprio_trace_replay);
    return UNITY_END();
}
