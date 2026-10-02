// Regressionstest fuer N-08: millis()-Rollover bei Deadline-Vergleichen.
//
// Warum diese Form: die betroffenen Vergleichsstellen (rebootAuto, DisplayOffWait,
// startTimeout, check_temperature, run, u32Next_m) sitzen in esp32_main.cpp,
// nrf52_main.cpp, gps_functions.cpp und weiteren plattformspezifischen Dateien, die
// wegen ihrer Arduino-/Hardware-Abhaengigkeiten nicht nativ uebersetzbar sind. Dieser
// Test bildet daher den Fehlermechanismus nach, nicht die Aufrufstelle selbst: er
// zeigt, dass der direkte Vergleich `millis() > deadline` nach einem Ueberlauf von
// millis() das falsche Ergebnis liefert, waehrend `(int32_t)(millis() - deadline) > 0`
// -- die an allen 21 Fundstellen angewandte Umformung -- korrekt bleibt.
//
//   pio test -e native

#include <unity.h>

#include <Arduino.h>

void setUp(void) {}
void tearDown(void) {}

// Unsicheres Original: direkter Vergleich, bricht am Ueberlauf.
static bool deadline_reached_unsafe(uint32_t now, uint32_t deadline)
{
    return now > deadline;
}

// Sichere Umformung: Subtraktion vor dem Vergleich, exakt die an allen
// Fundstellen angewandte Transformation `millis() <op> X` -> `(int32_t)(millis() - X) <op> 0`.
static bool deadline_reached_safe(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) > 0;
}

static void test_unsafe_vergleich_bricht_am_ueberlauf(void)
{
    // Deadline kurz vor dem Ueberlauf gesetzt (z.B. rebootAuto = millis() + 15000
    // bei millis() nahe UINT32_MAX), Pruefung erfolgt kurz nach dem Ueberlauf.
    uint32_t deadline = UINT32_MAX - 5000;     // z.B. millis()==UINT32_MAX-20000, +15000
    uint32_t now_after_wrap = 2000;            // millis() ist ueber 0 gewrappt

    TEST_ASSERT_TRUE_MESSAGE(now_after_wrap < deadline,
        "Testaufbau: now liegt zahlenmaessig VOR der Deadline (nach Wrap)");

    // Der Bug: die Deadline ist laengst ueberschritten, der direkte Vergleich sagt
    // aber "noch nicht erreicht", weil now_after_wrap < deadline als Zahl gilt.
    TEST_ASSERT_FALSE_MESSAGE(deadline_reached_unsafe(now_after_wrap, deadline),
        "Unsicherer Vergleich erkennt die ueberfaellige Deadline nach dem Wrap nicht");
}

static void test_sicherer_vergleich_uebersteht_ueberlauf(void)
{
    uint32_t deadline = UINT32_MAX - 5000;
    uint32_t now_after_wrap = 2000;

    // Die sichere Umformung erkennt dieselbe Situation korrekt als "erreicht".
    TEST_ASSERT_TRUE_MESSAGE(deadline_reached_safe(now_after_wrap, deadline),
        "Sicherer Vergleich muss die Deadline nach dem Wrap als erreicht erkennen");
}

static void test_sicherer_vergleich_ohne_wrap_bleibt_identisch(void)
{
    // Ohne Ueberlauf muessen unsicherer und sicherer Vergleich exakt uebereinstimmen --
    // die Umformung darf normales Verhalten nicht veraendern.
    uint32_t deadline = 100000;

    struct { uint32_t now; } cases[] = { {0}, {50000}, {99999}, {100000}, {100001}, {500000} };
    for (auto &c : cases)
    {
        TEST_ASSERT_EQUAL_MESSAGE(deadline_reached_unsafe(c.now, deadline),
                                   deadline_reached_safe(c.now, deadline),
                                   "Vergleichsergebnis muss ausserhalb des Wraps identisch sein");
    }
}

static void test_shim_uhr_simuliert_wrap(void)
{
    // Zeigt, dass der Shim selbst einen echten Ueberlauf abbilden kann, damit das
    // Szenario oben kein reines Zahlenspiel bleibt.
    mc_test_set_millis(UINT32_MAX - 500);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX - 500, millis());
    mc_test_advance_millis(1000);              // 500 vor Ueberlauf + 1000 -> wrapped
    TEST_ASSERT_EQUAL_UINT32(499, millis());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_shim_uhr_simuliert_wrap);
    RUN_TEST(test_unsafe_vergleich_bricht_am_ueberlauf);
    RUN_TEST(test_sicherer_vergleich_uebersteht_ueberlauf);
    RUN_TEST(test_sicherer_vergleich_ohne_wrap_bleibt_identisch);

    return UNITY_END();
}
