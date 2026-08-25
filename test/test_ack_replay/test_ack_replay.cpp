// Layer-B-Replay fuer den ACK-Pfad: haette isPlausibleAckFrame() Verkehr
// abgewiesen, den das Feld nachweislich verarbeitet hat?
//
// Die anderen beiden Replays vergleichen Entscheidung gegen Entscheidung.
// Hier geht das nicht: isPlausibleAckFrame() ist juenger als diese Logs
// (Nachtrag zu handleACK(), siehe ack_functions.h), es gibt also kein
// geloggtes Urteil zum Abgleich. Was es gibt, ist etwas Besseres als ein
// Urteil -- eine WIRKUNG: auf jeden Frame in ack_honoured.txt folgte im Log
// ein ACK_RECEIVED, das einen wartenden Ringslot geschlossen hat. Diese
// Frames waren also unstreitig echte ACKs.
//
// Damit wird die Frage praezise, die ein nachtraeglich eingezogener Filter
// immer aufwirft: schneidet er ins Fleisch? Verwirft er einen dieser Frames,
// haette er im Feld eine Quittung unterdrueckt und eine unnoetige
// Neuaussendung ausgeloest.
//
// Die Gegenrichtung -- verwirft der Filter genug Unfug? -- prueft
// test_aprs_fuzz gegen den vollen ACK-Korpus, und test_ack_validate gegen
// handverlesene Grenzfaelle.
//
//   pio test -e native -f test_ack_replay

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <ack_functions.h>

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

static uint16_t hex2bin(const char *hex, uint8_t *out, uint16_t maxlen)
{
    uint16_t n = 0;
    while (hex[0] && hex[1] && n < maxlen)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int h = nib(hex[0]), l = nib(hex[1]);
        if (h < 0 || l < 0) break;
        out[n++] = (uint8_t)((h << 4) | l);
        hex += 2;
    }
    return n;
}

// ---------------------------------------------------------------- Replay

static void test_filter_verwirft_keinen_honorierten_ack(void)
{
    FILE *f = openRel("test/support/traces/ack_honoured.txt", "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "ack_honoured.txt nicht gefunden (cwd?)");

    char line[512];
    int checked = 0, rejected = 0;

    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        char hex[128];
        if (sscanf(line, "%127s", hex) != 1) continue;

        uint8_t buf[64];
        uint16_t n = hex2bin(hex, buf, sizeof(buf));
        if (n == 0) continue;

        checked++;
        if (!isPlausibleAckFrame(buf, n, MAX_HOP_LIMIT))
        {
            rejected++;
            printf("  verworfen: %s  (byte5=%02X hop=%u)\n",
                   hex, buf[5], (unsigned)(buf[5] & 0x7F));
        }
    }
    fclose(f);

    printf("\n[ack] %d im Feld honorierte ACKs geprueft, %d wuerden heute verworfen\n",
           checked, rejected);

    TEST_ASSERT_GREATER_THAN_MESSAGE(10, checked,
        "zu wenige Frames -- Trace zu kurz oder Format geaendert");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rejected,
        "isPlausibleAckFrame() wuerde ein ACK verwerfen, das im Feld eine "
        "wartende Nachricht quittiert hat -- der Filter schneidet ins Fleisch");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_filter_verwirft_keinen_honorierten_ack);
    return UNITY_END();
}
