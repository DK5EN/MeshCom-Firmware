// Layer-B-Replay: die Dedup-Entscheidungsfolge echter Knoten gegen den echten
// Ring nachfahren.
//
// Das ist etwas anderes als die uebrigen Suiten. test_aprs_* prueft die
// Uebersetzung Bytes <-> Felder; hier geht es um VERHALTEN ueber die Zeit:
// welche Nachricht der Knoten als neu ansah, welche er als Duplikat verwarf,
// und in welchen Slot er sie schrieb. Die Vorlage ist die [MC-DBG]-Ausgabe
// laufender Knoten (test/support/traces/dedup_trace.txt, geerntet mit
// tools/traceharvest.py aus 48 Knotenstunden).
//
// Kein Modell, sondern der Code selbst: gefahren werden is_new_packet() und
// addLoraRxBuffer() aus dedup_functions.cpp. Eine Abweichung heisst deshalb,
// dass sich der Code vom Feld entfernt hat -- nicht, dass eine Nachbildung
// abgedriftet ist.
//
// Zwei Dinge muessen dafuer stimmen:
//
//   Startzustand  Der Knoten lief schon, als das Log begann, und er kann
//                 mittendrin neu gestartet sein -- beides setzt den
//                 Schreibzeiger auf einen Stand, den unsere Historie nicht
//                 hergibt. Der Trace fuehrt solche Spruenge als R-Marke
//                 (traceharvest.py erkennt sie daran, dass der geloggte Slot
//                 nicht +1 zum vorigen ist). Darauf wird der Ring geleert und
//                 der Zeiger neu gesetzt.
//
//   Aufwaermen    Ein DUP kann auf einen Eintrag zeigen, der vor der letzten
//                 R-Marke in den Ring kam -- den haben wir nicht. DUP/NEW-
//                 Urteile werden daher erst geprueft, wenn der Ring seither
//                 einmal komplett aus unserer eigenen Historie gefuellt ist
//                 (MAX_DEDUP_RING ADDs).
//
//   pio test -e native_dedup

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <dedup_functions.h>

// dedup_functions.cpp erwartet dieses Flag (Definition sonst in
// loop_functions.cpp). Aus: die [MC-DBG]-Zeilen des Moduls interessieren hier
// nicht, nur seine Entscheidungen.
bool bLORADEBUG = false;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- Helfer

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

static void resetRing(void)
{
    memset(ringBufferLoraRX, 0x00, sizeof(ringBufferLoraRX));
    loraWrite.store(0);
}

// msg_id in Speicherreihenfolge (little endian), so wie der Empfangspfad sie
// an is_new_packet() reicht (RcvBuffer+1).
static void idToBuf(uint32_t id, uint8_t out[4])
{
    out[0] = (uint8_t)(id & 0xFF);
    out[1] = (uint8_t)((id >> 8) & 0xFF);
    out[2] = (uint8_t)((id >> 16) & 0xFF);
    out[3] = (uint8_t)((id >> 24) & 0xFF);
}

// ---------------------------------------------------------------- Replay

static void test_dedup_trace_replay(void)
{
    FILE *f = openRel("test/support/traces/dedup_trace.txt", "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "dedup_trace.txt nicht gefunden (cwd?)");

    char line[256];
    char cur_node[96] = "";
    long adds_this_node = 0;

    long total = 0, checked_verdict = 0, checked_slot = 0, warmup = 0, resyncs = 0;
    long bad_verdict = 0, bad_slot = 0, shown_v = 0, shown_s = 0;

    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char node[96], kind[4], idhex[16];
        int slot = 0, srv = 0;
        int n = sscanf(line, "%95s %3s %15s %d %d", node, kind, idhex, &slot, &srv);
        if (n < 3) continue;

        if (strcmp(node, cur_node) != 0)
        {
            // Neuer Knoten, neuer Ring -- Zustand darf nicht ueberschwappen.
            snprintf(cur_node, sizeof(cur_node), "%s", node);
            resetRing();
            adds_this_node = 0;
        }

        if (kind[0] == 'R')
        {
            // Neustart des Knotens: Ring leeren, Zeiger auf den geloggten
            // Stand, Aufwaermzaehler zuruecksetzen.
            resetRing();
            loraWrite.store((uint8_t)slot);
            adds_this_node = 0;
            resyncs++;
            total++;
            continue;
        }

        uint32_t id = (uint32_t)strtoul(idhex, nullptr, 16);
        uint8_t buf[4];
        idToBuf(id, buf);
        total++;

        // Der Ring ist erst dann vollstaendig aus unserer Historie gefuellt,
        // wenn er einmal umgelaufen ist. Vorher sind Urteile nicht vergleichbar.
        bool warm = (adds_this_node >= MAX_DEDUP_RING);

        if (kind[0] == 'N' || kind[0] == 'D')
        {
            bool got = is_new_packet(buf);
            bool want = (kind[0] == 'N');
            if (!warm) { warmup++; continue; }

            checked_verdict++;
            if (got != want)
            {
                bad_verdict++;
                if (shown_v++ < 10)
                    printf("  %s x%08X: Feld sagte %s, Code sagt %s\n",
                           node, id, want ? "NEU" : "DUP", got ? "NEU" : "DUP");
                continue;
            }
            // Beim Duplikat muss auch der Fundort stimmen -- sonst hat sich die
            // Belegung verschoben, ohne dass das Urteil es verraet.
            if (kind[0] == 'D')
            {
                int found = checkOwnRx(buf);
                checked_slot++;
                if (found != slot)
                {
                    bad_slot++;
                    if (shown_s++ < 5)
                        printf("  %s x%08X: Feld fand Slot %d, Code findet %d\n",
                               node, id, slot, found);
                }
            }
        }
        else if (kind[0] == 'A')
        {
            uint8_t before = loraWrite.load();
            addLoraRxBuffer(id, srv != 0);
            adds_this_node++;

            checked_slot++;
            if (before != (uint8_t)slot)
            {
                bad_slot++;
                if (shown_s++ < 5)
                    printf("  %s x%08X: Feld schrieb Slot %d, Code schrieb %d\n",
                           node, id, slot, before);
            }
        }
    }
    fclose(f);

    printf("\n[dedup] %ld Ereignisse, %ld Neustarts, %ld waehrend Aufwaermphase uebersprungen\n",
           total, resyncs, warmup);
    printf("[dedup] %ld Urteile geprueft (%ld abweichend), "
           "%ld Slots geprueft (%ld abweichend)\n",
           checked_verdict, bad_verdict, checked_slot, bad_slot);

    TEST_ASSERT_GREATER_THAN_MESSAGE(1000, checked_verdict,
        "zu wenige vergleichbare Ereignisse -- Trace zu kurz oder Format geaendert");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad_verdict,
        "is_new_packet() entscheidet anders als der Knoten im Feld");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad_slot,
        "die Slotbelegung des Rings weicht vom Feld ab");
}

// Die Ringgroesse ist eine Feldentscheidung (siehe dedup_functions.h): 100
// bleibt, weil ein groesserer Ring praktisch keine echten Duplikate mehr
// faengt, ab etwa 500 aber in die msg_id-Wiederverwendung hineinreicht und
// legitime Nachrichten unterdrueckt. Wer die Zahl aendert, aendert das
// Verhalten im Netz -- und laesst hier absichtlich einen Test scheitern.
static void test_ringgroesse_ist_eine_bewusste_entscheidung(void)
{
    TEST_ASSERT_EQUAL_INT_MESSAGE(100, MAX_DEDUP_RING,
        "MAX_DEDUP_RING geaendert -- Begruendung in dedup_functions.h lesen und "
        "die Feldmessung wiederholen, bevor dieser Wert angepasst wird");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_dedup_trace_replay);
    RUN_TEST(test_ringgroesse_ist_eine_bewusste_entscheidung);
    return UNITY_END();
}
