// Native Testsuite fuer den 0.2-Re-ACK-Limiter (docs/dm-transport-impl-plan-
// 20260913.md, Stage 0 §0.2).
//
//   pio test -e native -f test_reack_limiter
//
// reackAllowed() begrenzt re-ACKs auf ein wiederholtes (Rufzeichen, NNN)-Paar
// auf eines je REACK_LIMITER_WINDOW_MS ms -- ohne das koennte ein wiedereingespieltes Frame zu
// einem Re-ACK-Sturm fuehren. Geprueft wird das Fenster selbst, Unabhaengig-
// keit ueber verschiedene Rufzeichen/NNN, die Verdraengung des aeltesten
// Eintrags bei 8 belegten Slots und der millis()-Rollover.

#include <unity.h>

#include <stdint.h>

#include <reack_limiter.h>

void setUp(void)
{
    reackLimiterReset();
}

void tearDown(void) {}

static void test_erster_aufruf_erlaubt(void)
{
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, 1000));
}

static void test_gleiches_paar_innerhalb_fenster_verboten(void)
{
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, 1000));
    TEST_ASSERT_FALSE(reackAllowed("DK5EN-1", 42, 1000 + (REACK_LIMITER_WINDOW_MS - 1)));
}

static void test_gleiches_paar_bei_REACK_LIMITER_WINDOW_MSms_wieder_erlaubt(void)
{
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, 1000));
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, 1000 + REACK_LIMITER_WINDOW_MS));
}

static void test_andere_nnn_gleiches_rufzeichen_erlaubt(void)
{
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, 1000));
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 43, 1000));
}

static void test_anderes_rufzeichen_gleiche_nnn_erlaubt(void)
{
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, 1000));
    TEST_ASSERT_TRUE(reackAllowed("OE1XYZ", 42, 1000));
}

// Der 9. unterschiedliche Eintrag verdraengt den aeltesten (Slot 0); dessen
// Paar ist danach wieder sofort (ohne Fensterablauf) erlaubt, weil sein
// Zustand aus der Tabelle gefallen ist.
static void test_neunter_eintrag_verdraengt_den_aeltesten(void)
{
    for(int i = 0; i < 8; i++)
        TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", (uint16_t)i, 1000));

    // Tabelle voll: das 9. distincte Paar verdraengt Slot 0 (nnn=0).
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 8, 1000));

    // nnn=0 ist aus der Tabelle gefallen -- sofort wieder erlaubt, obwohl
    // das 60s-Fenster nicht abgelaufen ist. (Dieser Aufruf traegt nnn=0
    // seinerseits wieder ein -- Slot 1 (nnn=1) waere der naechste, den ein
    // 11. distinctes Paar verdraengen wuerde; das ist hier nicht Teil der
    // Behauptung.)
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 0, 1001));

    // nnn=2..7 sind unberuehrt und weiterhin im Fenster gesperrt.
    for(int i = 2; i < 8; i++)
        TEST_ASSERT_FALSE(reackAllowed("DK5EN-1", (uint16_t)i, 1001));
}

static void test_millis_rollover(void)
{
    uint32_t near_wrap = 0xFFFFFFF0UL;
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, near_wrap));

    // 32 ms nach dem Wrap: 32 ms seit dem letzten Allow, klar innerhalb des Fensters.
    uint32_t wrapped = 0x00000010UL;
    TEST_ASSERT_FALSE(reackAllowed("DK5EN-1", 42, wrapped));

    // REACK_LIMITER_WINDOW_MS ms nach dem urspruenglichen (vor dem Wrap) Zeitstempel wieder
    // erlaubt.
    uint32_t after_window = (uint32_t)(near_wrap + REACK_LIMITER_WINDOW_MS);
    TEST_ASSERT_TRUE(reackAllowed("DK5EN-1", 42, after_window));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_erster_aufruf_erlaubt);
    RUN_TEST(test_gleiches_paar_innerhalb_fenster_verboten);
    RUN_TEST(test_gleiches_paar_bei_REACK_LIMITER_WINDOW_MSms_wieder_erlaubt);
    RUN_TEST(test_andere_nnn_gleiches_rufzeichen_erlaubt);
    RUN_TEST(test_anderes_rufzeichen_gleiche_nnn_erlaubt);
    RUN_TEST(test_neunter_eintrag_verdraengt_den_aeltesten);
    RUN_TEST(test_millis_rollover);
    return UNITY_END();
}
