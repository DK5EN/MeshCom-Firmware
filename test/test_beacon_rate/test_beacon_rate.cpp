// Native Testsuite fuer beaconShotAllowed() -- die Untergrenze des
// Sofort-Positions-Pfades (sendPosition() mit uintervall == 0x9999).
//
// Hintergrund: mcmap-Rohlog interlink, 2026-08-30, DL6MDF-11 -- 25.146
// Positionsrahmen in 21 Minuten (~20/s) aus einem Knoten, dessen regulaere
// Kadenz ein Rahmen alle 30 Minuten ist. Der Sofort-Pfad kannte bis dahin
// keinen Mindestabstand; jeder Ausloeser (--sendpos, User-Button, und vor
// allem die unauthentifizierte EXTUDP-Telemetrie-Injektion) erzeugte genau
// einen Beacon, egal wie kurz der letzte her war. Details: src/beacon_rate.h.
//
//   pio test -e native -f test_beacon_rate

#include <unity.h>

#include <beacon_rate.h>

void setUp(void) {}
void tearDown(void) {}

// Der allererste Beacon nach dem Boot geht immer hinaus.
static void test_first_shot_always_allowed(void)
{
    TEST_ASSERT_TRUE(beaconShotAllowed(0, 0, false, BEACON_SHOT_MIN_MS));
    TEST_ASSERT_TRUE(beaconShotAllowed(1234567, 0, false, BEACON_SHOT_MIN_MS));
}

// Der Feldfall: ~20 Ausloeser pro Sekunde. Nur der erste darf durch.
static void test_flood_is_suppressed(void)
{
    uint32_t last = 100000;
    // 50 ms Abstand, 400 Versuche = 20 s Dauerfeuer
    for(int i = 1; i <= 400; i++)
    {
        uint32_t now = last + (uint32_t)(i * 50);
        TEST_ASSERT_FALSE(beaconShotAllowed(now, last, true, BEACON_SHOT_MIN_MS));
    }
}

// Genau auf der Schranke ist erlaubt, eine Millisekunde davor nicht.
static void test_boundary(void)
{
    uint32_t last = 500000;
    TEST_ASSERT_FALSE(beaconShotAllowed(last + BEACON_SHOT_MIN_MS - 1, last, true, BEACON_SHOT_MIN_MS));
    TEST_ASSERT_TRUE (beaconShotAllowed(last + BEACON_SHOT_MIN_MS,     last, true, BEACON_SHOT_MIN_MS));
    TEST_ASSERT_TRUE (beaconShotAllowed(last + BEACON_SHOT_MIN_MS + 1, last, true, BEACON_SHOT_MIN_MS));
}

// Die regulaere Kadenz des betroffenen Knotens (30 min) bleibt unberuehrt,
// ebenso ein von Hand abgesetztes --sendpos nach einer Minute.
static void test_regular_cadence_passes(void)
{
    uint32_t last = 7000;
    TEST_ASSERT_TRUE(beaconShotAllowed(last + 60UL * 1000UL,        last, true, BEACON_SHOT_MIN_MS));
    TEST_ASSERT_TRUE(beaconShotAllowed(last + 30UL * 60UL * 1000UL, last, true, BEACON_SHOT_MIN_MS));
}

// millis()-Ueberlauf nach 49,7 Tagen: der letzte Beacon liegt vor dem Wrap,
// jetzt ist danach. Die uint32_t-Differenz muss die echte Wartezeit liefern
// und darf weder blockieren (Differenz "riesig" -> immer erlaubt waere falsch
// herum) noch faelschlich sperren.
static void test_millis_rollover(void)
{
    uint32_t last = 0xFFFFFF00UL;              // 256 ms vor dem Ueberlauf

    // 1 s spaeter (also 744 ms nach dem Wrap): noch gesperrt
    TEST_ASSERT_FALSE(beaconShotAllowed(last + 1000UL, last, true, BEACON_SHOT_MIN_MS));

    // 30 s spaeter: erlaubt
    TEST_ASSERT_TRUE(beaconShotAllowed(last + BEACON_SHOT_MIN_MS, last, true, BEACON_SHOT_MIN_MS));
}

// Die Schranke ist parametrisiert, damit sie sich ohne Neubau der Logik
// verstellen laesst.
static void test_min_ms_is_honoured(void)
{
    uint32_t last = 10000;
    TEST_ASSERT_TRUE (beaconShotAllowed(last + 5000, last, true, 5000));
    TEST_ASSERT_FALSE(beaconShotAllowed(last + 5000, last, true, 5001));
    TEST_ASSERT_TRUE (beaconShotAllowed(last,        last, true, 0));
}

// FL-02: derselbe Sofort-Pfad-Mindestabstand fuer sendHeyShot() (--sendhey).
// sendHeyShot() selbst lebt in loop_functions.cpp und ist nativ nicht
// uebersetzbar (Arduino-Abhaengigkeiten), aber sie ruft beaconShotAllowed()
// mit ihrem EIGENEN Zeitstempel (lastOwnHeyTx) auf -- unabhaengig von
// lastOwnPosTx des Positions-Pfades. Diese Tests bilden genau das mit zwei
// getrennten Zustandsvariablen nach, so wie es in loop_functions.cpp steht.

// Zwei --sendhey innerhalb von 30 s: der zweite wird unterdrueckt.
static void test_hey_second_shot_within_window_suppressed(void)
{
    uint32_t lastOwnHeyTx = 200000;
    bool bHaveOwnHeyTx = true;

    TEST_ASSERT_FALSE(beaconShotAllowed(lastOwnHeyTx + 5000, lastOwnHeyTx, bHaveOwnHeyTx, BEACON_SHOT_MIN_MS));
    TEST_ASSERT_FALSE(beaconShotAllowed(lastOwnHeyTx + BEACON_SHOT_MIN_MS - 1, lastOwnHeyTx, bHaveOwnHeyTx, BEACON_SHOT_MIN_MS));
}

// Nach 30 s ist der naechste --sendhey wieder erlaubt.
static void test_hey_shot_after_window_allowed(void)
{
    uint32_t lastOwnHeyTx = 200000;
    bool bHaveOwnHeyTx = true;

    TEST_ASSERT_TRUE(beaconShotAllowed(lastOwnHeyTx + BEACON_SHOT_MIN_MS, lastOwnHeyTx, bHaveOwnHeyTx, BEACON_SHOT_MIN_MS));
}

// Der HEY-Zeitstempel ist eigenstaendig: ein kuerzlich gesendeter POS-Beacon
// sperrt einen faelligen HEY-Sofort-Beacon nicht, und umgekehrt.
static void test_hey_and_pos_timestamps_are_independent(void)
{
    uint32_t lastOwnPosTx = 100000;
    uint32_t lastOwnHeyTx = 100000 - BEACON_SHOT_MIN_MS; // HEY-Fenster ist schon abgelaufen
    uint32_t now = 100000;

    // POS gerade erst gesendet -> ein weiterer POS-Sofort-Beacon ist gesperrt.
    TEST_ASSERT_FALSE(beaconShotAllowed(now, lastOwnPosTx, true, BEACON_SHOT_MIN_MS));

    // HEY-Fenster ist unabhaengig davon bereits wieder offen.
    TEST_ASSERT_TRUE(beaconShotAllowed(now, lastOwnHeyTx, true, BEACON_SHOT_MIN_MS));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_shot_always_allowed);
    RUN_TEST(test_flood_is_suppressed);
    RUN_TEST(test_boundary);
    RUN_TEST(test_regular_cadence_passes);
    RUN_TEST(test_millis_rollover);
    RUN_TEST(test_min_ms_is_honoured);
    RUN_TEST(test_hey_second_shot_within_window_suppressed);
    RUN_TEST(test_hey_shot_after_window_allowed);
    RUN_TEST(test_hey_and_pos_timestamps_are_independent);
    return UNITY_END();
}
