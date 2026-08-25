// Nativer Test fuer den Mitschnitt-Ring (src/capture_functions.cpp).
//
// Warum eigener Test: der Ring ist handgeschriebene Modulo-Arithmetik mit
// Umlauf, variabler Satzlaenge und einer Voll-Erkennung, die ein Byte frei
// laesst. Genau dort sitzt ein Off-by-one gern und faellt auf der Hardware
// erst nach Stunden auf -- als ein Frame, den niemand vermisst, weil der Ring
// ja "irgendwie laeuft".
//
// Nicht abgedeckt: das Wettrennen der beiden Produzenten (OnRxDone im
// Timer-Task, doTX im Loop-Task auf nRF52). Das braucht Praeemption, die es
// nativ nicht gibt. Der try-lock, der es abfaengt, ist in
// capture_functions.cpp begruendet; hier wird nur geprueft, dass ein
// vollstaendig geschriebener Satz unversehrt wieder herauskommt.
//
//   pio test -e native_capture

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <capture_functions.h>

void setUp(void) {}
void tearDown(void) {}

#define LINE_MAX (2 * UDP_TX_BUF_SIZE + 96)

// Ring leerraeumen, damit jeder Test vom selben Zustand startet. Der Ring ist
// modulintern statisch -- Reihenfolge der Tests darf keine Rolle spielen.
static void drainAll(void)
{
    char line[LINE_MAX];
    int guard = 0;
    while (captureFormatNext(line, sizeof(line)))
    {
        TEST_ASSERT_LESS_THAN_MESSAGE(4096, ++guard, "captureFormatNext laeuft nicht leer");
    }
}

static void fillPattern(uint8_t *buf, uint16_t len, uint8_t seed)
{
    for (uint16_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(seed + i);
}

// ---------------------------------------------------------------------------

static void test_leerer_ring_liefert_nichts(void)
{
    drainAll();
    char line[LINE_MAX];
    TEST_ASSERT_FALSE(captureFormatNext(line, sizeof(line)));
}

static void test_rx_satz_kommt_unversehrt_zurueck(void)
{
    drainAll();

    uint8_t frame[16];
    fillPattern(frame, sizeof(frame), 0x40);
    captureFrame('R', frame, sizeof(frame), -109, -7);

    char line[LINE_MAX];
    TEST_ASSERT_TRUE(captureFormatNext(line, sizeof(line)));

    // Negative RSSI/SNR muessen das Byte-Zerlegen im Ring ueberleben.
    TEST_ASSERT_EQUAL_STRING(
        "[MC-TEST] RX_FRAME len=16 rssi=-109 snr=-7 "
        "hex=404142434445464748494A4B4C4D4E4F", line);

    TEST_ASSERT_FALSE(captureFormatNext(line, sizeof(line)));
}

static void test_tx_satz_traegt_kein_rssi(void)
{
    drainAll();

    uint8_t frame[4] = { 0x3A, 0x01, 0x02, 0x03 };
    captureFrame('T', frame, sizeof(frame), 0, 0);

    char line[LINE_MAX];
    TEST_ASSERT_TRUE(captureFormatNext(line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING("[MC-TEST] TX_FRAME len=4 hex=3A010203", line);
}

static void test_reihenfolge_bleibt_erhalten(void)
{
    drainAll();

    for (int i = 0; i < 5; i++)
    {
        uint8_t frame[8];
        fillPattern(frame, sizeof(frame), (uint8_t)(0x10 * (i + 1)));
        captureFrame('R', frame, sizeof(frame), (int16_t)(-50 - i), (int8_t)i);
    }

    for (int i = 0; i < 5; i++)
    {
        char line[LINE_MAX], want[LINE_MAX];
        TEST_ASSERT_TRUE(captureFormatNext(line, sizeof(line)));
        snprintf(want, sizeof(want), "[MC-TEST] RX_FRAME len=8 rssi=%d snr=%d hex=", -50 - i, i);
        TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(want, line, strlen(want), "FIFO-Reihenfolge verletzt");
    }
    char line[LINE_MAX];
    TEST_ASSERT_FALSE(captureFormatNext(line, sizeof(line)));
}

// Der eigentliche Grund fuer diesen Test: nach vielen Durchlaeufen liegt der
// Schreibzeiger irgendwo, und ein Satz muss ueber die Ringgrenze hinweg
// geschrieben und wieder gelesen werden koennen.
static void test_umlauf_ueber_die_ringgrenze(void)
{
    drainAll();

    uint8_t frame[100];
    char line[LINE_MAX];

    // 100-Byte-Frames + 6 Byte Kopf: nach wenigen Runden ist die Ringgrenze
    // mehrfach ueberschritten.
    for (int round = 0; round < 40; round++)
    {
        fillPattern(frame, sizeof(frame), (uint8_t)round);
        captureFrame('R', frame, sizeof(frame), (int16_t)(-40 - round), 3);

        TEST_ASSERT_TRUE_MESSAGE(captureFormatNext(line, sizeof(line)),
                                 "Satz nach Umlauf verschwunden");

        char want[LINE_MAX];
        int off = snprintf(want, sizeof(want), "[MC-TEST] RX_FRAME len=100 rssi=%d snr=3 hex=",
                           -40 - round);
        for (int i = 0; i < 100; i++)
            off += snprintf(want + off, sizeof(want) - off, "%02X", (uint8_t)(round + i));

        TEST_ASSERT_EQUAL_STRING_MESSAGE(want, line, "Nutzdaten nach Umlauf verfaelscht");
    }
}

// Voller Ring: der ueberzaehlige Frame muss verworfen UND gezaehlt werden --
// und die Zaehlung muss beim Leerlaufen herauskommen. Ein stiller Verlust
// waere schlimmer als gar kein Mitschnitt: der Korpus behauptete dann
// Vollstaendigkeit, die er nicht hat.
static void test_ueberlauf_wird_gezaehlt_und_gemeldet(void)
{
    drainAll();

    uint8_t frame[UDP_TX_BUF_SIZE];
    fillPattern(frame, sizeof(frame), 0xA0);

    // 768 Byte Ring, Satz = 6 + 255 = 261 Byte -> zwei passen, der dritte
    // nicht mehr (2*261 = 522, frei sind 767).
    captureFrame('R', frame, sizeof(frame), -1, -1);
    captureFrame('R', frame, sizeof(frame), -2, -2);
    captureFrame('R', frame, sizeof(frame), -3, -3);   // muss verworfen werden

    char line[LINE_MAX];
    TEST_ASSERT_TRUE(captureFormatNext(line, sizeof(line)));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "rssi=-1 "), "erster Satz fehlt");

    TEST_ASSERT_TRUE(captureFormatNext(line, sizeof(line)));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "rssi=-2 "), "zweiter Satz fehlt");

    // Ring leer -> jetzt muss die Verlustmeldung kommen.
    TEST_ASSERT_TRUE_MESSAGE(captureFormatNext(line, sizeof(line)),
                             "Verlustmeldung fehlt");
    TEST_ASSERT_EQUAL_STRING("[MC-TEST] CAPTURE_DROPPED n=1", line);

    // ... und danach Ruhe.
    TEST_ASSERT_FALSE(captureFormatNext(line, sizeof(line)));
}

static void test_leere_und_ungueltige_eingaben(void)
{
    drainAll();

    uint8_t frame[4] = { 1, 2, 3, 4 };
    captureFrame('R', NULL, 4, 0, 0);        // kein Puffer
    captureFrame('R', frame, 0, 0, 0);       // Laenge 0

    char line[LINE_MAX];
    TEST_ASSERT_FALSE_MESSAGE(captureFormatNext(line, sizeof(line)),
                              "ungueltige Eingabe darf keinen Satz erzeugen");

    // Auch der Konsument muss Unsinn abweisen, ohne zu schreiben.
    TEST_ASSERT_FALSE(captureFormatNext(NULL, 16));
    TEST_ASSERT_FALSE(captureFormatNext(line, 0));
}

// Zu kleiner Ausgabepuffer: der Satz darf verkuerzt werden, aber der Ring muss
// weiterruecken. Bliebe er stehen, laege derselbe Satz beim naechsten Aufruf
// wieder an -- der Mitschnitt haenge fest und wiederholte ewig eine Zeile.
static void test_zu_kleiner_puffer_haengt_den_ring_nicht_auf(void)
{
    drainAll();

    uint8_t frame[32];
    fillPattern(frame, sizeof(frame), 0x70);
    captureFrame('R', frame, sizeof(frame), -60, 4);
    captureFrame('T', frame, sizeof(frame), 0, 0);

    // Platz fuer den Kopf, aber nicht fuer alle 64 Hexzeichen. Der Puffer darf
    // bis auf das letzte Byte gefuellt werden, aber keines darueber hinaus --
    // ASan im Testenvironment faengt einen Ueberlauf zusaetzlich ab.
    char small[60];
    TEST_ASSERT_TRUE(captureFormatNext(small, sizeof(small)));
    TEST_ASSERT_LESS_THAN_MESSAGE(sizeof(small), strlen(small), "Puffer ueberschrieben");

    // Der zweite Satz muss jetzt kommen -- nicht noch einmal der erste.
    char line[LINE_MAX];
    TEST_ASSERT_TRUE(captureFormatNext(line, sizeof(line)));
    TEST_ASSERT_EQUAL_STRING(
        "[MC-TEST] TX_FRAME len=32 "
        "hex=707172737475767778797A7B7C7D7E7F808182838485868788898A8B8C8D8E8F", line);

    TEST_ASSERT_FALSE(captureFormatNext(line, sizeof(line)));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_leerer_ring_liefert_nichts);
    RUN_TEST(test_rx_satz_kommt_unversehrt_zurueck);
    RUN_TEST(test_tx_satz_traegt_kein_rssi);
    RUN_TEST(test_reihenfolge_bleibt_erhalten);
    RUN_TEST(test_umlauf_ueber_die_ringgrenze);
    RUN_TEST(test_ueberlauf_wird_gezaehlt_und_gemeldet);
    RUN_TEST(test_leere_und_ungueltige_eingaben);
    RUN_TEST(test_zu_kleiner_puffer_haengt_den_ring_nicht_auf);
    return UNITY_END();
}
