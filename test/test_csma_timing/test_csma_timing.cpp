// Native Testsuite fuer die CSMA-Backoff-Arithmetik aus src/csma_timing.h (W0.6).
//
// Hintergrund: csma_compute_timeout_prio() (lora_functions.cpp) zieht RadioLib mit
// und steht in keinem nativen build_src_filter, die Zahlentabelle (Basis und Slots
// je Prioritaet, 5/6- und 2/3-Skalierung der Wiederholungen, Rapid-fire ab
// CSMA_MAX_ATTEMPTS) hatte deshalb keinen Host-Test. Die reine Arithmetik liegt
// jetzt in csma_timing.h; die Zufallsziehung bleibt im Aufrufer, getestet wird
// daher mit explizitem jitter_slot. Erwartungswerte kommen aus den Makros, nicht
// aus der Funktion.
//
//   pio test -e native -f test_csma_timing

#include <unity.h>

#include <csma_timing.h>

void setUp(void)    {}
void tearDown(void) {}

struct PrioRow { uint8_t prio; unsigned long base; int slots; };

// Reihenfolge CRITICAL..BACKGROUND, Werte direkt aus den CSMA_PRIO_*-Makros.
static const PrioRow ROWS[] = {
    { MSG_PRIO_CRITICAL,   CSMA_PRIO_BASE_1, CSMA_PRIO_SLOTS_1 },
    { MSG_PRIO_HIGH,       CSMA_PRIO_BASE_2, CSMA_PRIO_SLOTS_2 },
    { MSG_PRIO_NORMAL,     CSMA_PRIO_BASE_3, CSMA_PRIO_SLOTS_3 },
    { MSG_PRIO_LOW,        CSMA_PRIO_BASE_4, CSMA_PRIO_SLOTS_4 },
    { MSG_PRIO_BACKGROUND, CSMA_PRIO_BASE_5, CSMA_PRIO_SLOTS_5 },
};
static const int NROWS = (int)(sizeof(ROWS) / sizeof(ROWS[0]));

static unsigned long scaled(unsigned long base, int attempt)
{
    if (attempt >= 2) return base * 2 / 3;
    if (attempt >= 1) return base * 5 / 6;
    return base;
}

static void test_slots_je_prioritaet(void)
{
    for (int i = 0; i < NROWS; i++)
        TEST_ASSERT_EQUAL_INT(ROWS[i].slots, csmaSlotsForPrio(ROWS[i].prio));
}

static void test_slots_default_ist_normal(void)
{
    TEST_ASSERT_EQUAL_INT(CSMA_PRIO_SLOTS_3, csmaSlotsForPrio(0));
    TEST_ASSERT_EQUAL_INT(CSMA_PRIO_SLOTS_3, csmaSlotsForPrio(6));
    TEST_ASSERT_EQUAL_INT(CSMA_PRIO_SLOTS_3, csmaSlotsForPrio(255));
}

static void test_basis_je_prioritaet_und_versuch_ohne_jitter(void)
{
    for (int i = 0; i < NROWS; i++)
        for (int attempt = 0; attempt < CSMA_MAX_ATTEMPTS; attempt++)
            TEST_ASSERT_EQUAL_UINT32(scaled(ROWS[i].base, attempt),
                                     csmaTimeoutPrio(attempt, ROWS[i].prio, 0));
}

static void test_jitter_max_addiert_slots_mal_slotgroesse(void)
{
    for (int i = 0; i < NROWS; i++)
        for (int attempt = 0; attempt < CSMA_MAX_ATTEMPTS; attempt++)
        {
            unsigned long lo = scaled(ROWS[i].base, attempt);
            TEST_ASSERT_EQUAL_UINT32(lo + (unsigned long)ROWS[i].slots * CSMA_SLOT_SIZE,
                                     csmaTimeoutPrio(attempt, ROWS[i].prio, ROWS[i].slots));
        }
}

static void test_jitter_ist_linear_in_slotgroesse(void)
{
    for (long j = 0; j <= CSMA_PRIO_SLOTS_3; j++)
        TEST_ASSERT_EQUAL_UINT32((unsigned long)CSMA_PRIO_BASE_3 + (unsigned long)j * CSMA_SLOT_SIZE,
                                 csmaTimeoutPrio(0, MSG_PRIO_NORMAL, j));
}

static void test_default_prio_verhaelt_sich_wie_normal(void)
{
    const uint8_t unknown[] = { 0, 6, 7, 128, 255 };
    for (unsigned k = 0; k < sizeof(unknown); k++)
        for (int attempt = 0; attempt < CSMA_MAX_ATTEMPTS; attempt++)
        {
            TEST_ASSERT_EQUAL_UINT32(csmaTimeoutPrio(attempt, MSG_PRIO_NORMAL, 0),
                                     csmaTimeoutPrio(attempt, unknown[k], 0));
            TEST_ASSERT_EQUAL_UINT32(scaled(CSMA_PRIO_BASE_3, attempt) +
                                         (unsigned long)CSMA_PRIO_SLOTS_3 * CSMA_SLOT_SIZE,
                                     csmaTimeoutPrio(attempt, unknown[k], CSMA_PRIO_SLOTS_3));
        }
}

static void test_skalierung_5_6_und_2_3_mit_ganzzahl_abschnitt(void)
{
    // Echte Basiswerte; ganzzahlige Division schneidet ab (kein Runden).
    TEST_ASSERT_EQUAL_UINT32(4500, csmaTimeoutPrio(0, MSG_PRIO_NORMAL, 0));
    TEST_ASSERT_EQUAL_UINT32(3750, csmaTimeoutPrio(1, MSG_PRIO_NORMAL, 0));   // 4500*5/6
    TEST_ASSERT_EQUAL_UINT32(3000, csmaTimeoutPrio(2, MSG_PRIO_NORMAL, 0));   // 4500*2/3

    TEST_ASSERT_EQUAL_UINT32(3000, csmaTimeoutPrio(0, MSG_PRIO_CRITICAL, 0));
    TEST_ASSERT_EQUAL_UINT32(2500, csmaTimeoutPrio(1, MSG_PRIO_CRITICAL, 0)); // 3000*5/6
    TEST_ASSERT_EQUAL_UINT32(2000, csmaTimeoutPrio(2, MSG_PRIO_CRITICAL, 0)); // 3000*2/3

    TEST_ASSERT_EQUAL_UINT32(5500, csmaTimeoutPrio(0, MSG_PRIO_LOW, 0));
    TEST_ASSERT_EQUAL_UINT32(4583, csmaTimeoutPrio(1, MSG_PRIO_LOW, 0));      // 5500*5/6 = 4583.33
    TEST_ASSERT_EQUAL_UINT32(3666, csmaTimeoutPrio(2, MSG_PRIO_LOW, 0));      // 5500*2/3 = 3666.67
}

static void test_rapid_fire_ab_max_attempts_ignoriert_prio_und_jitter(void)
{
    for (int attempt = CSMA_MAX_ATTEMPTS; attempt <= CSMA_MAX_ATTEMPTS + 5; attempt++)
        for (int i = 0; i < NROWS; i++)
        {
            TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaTimeoutPrio(attempt, ROWS[i].prio, 0));
            TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaTimeoutPrio(attempt, ROWS[i].prio, ROWS[i].slots));
            TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaTimeoutPrio(attempt, ROWS[i].prio, 1000));
        }
    TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaTimeoutPrio(CSMA_MAX_ATTEMPTS, 0, 7));
    TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaTimeoutPrio(1000000, 255, 7));
}

static void test_prioritaetsreihenfolge_bei_versuch_0(void)
{
    // CRITICAL <= HIGH <= NORMAL <= LOW <= BACKGROUND (kleinere Zahl = frueher dran)
    for (int i = 0; i + 1 < NROWS; i++)
        TEST_ASSERT_TRUE(csmaTimeoutPrio(0, ROWS[i].prio, 0) <=
                         csmaTimeoutPrio(0, ROWS[i + 1].prio, 0));
    TEST_ASSERT_TRUE(csmaTimeoutPrio(0, MSG_PRIO_CRITICAL, 0) <
                     csmaTimeoutPrio(0, MSG_PRIO_BACKGROUND, 0));
}

static void test_wiederholung_wird_nie_laenger(void)
{
    for (int i = 0; i < NROWS; i++)
        for (int attempt = 1; attempt < CSMA_MAX_ATTEMPTS; attempt++)
            TEST_ASSERT_TRUE(csmaTimeoutPrio(attempt, ROWS[i].prio, 0) <=
                             csmaTimeoutPrio(attempt - 1, ROWS[i].prio, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_slots_je_prioritaet);
    RUN_TEST(test_slots_default_ist_normal);
    RUN_TEST(test_basis_je_prioritaet_und_versuch_ohne_jitter);
    RUN_TEST(test_jitter_max_addiert_slots_mal_slotgroesse);
    RUN_TEST(test_jitter_ist_linear_in_slotgroesse);
    RUN_TEST(test_default_prio_verhaelt_sich_wie_normal);
    RUN_TEST(test_skalierung_5_6_und_2_3_mit_ganzzahl_abschnitt);
    RUN_TEST(test_rapid_fire_ab_max_attempts_ignoriert_prio_und_jitter);
    RUN_TEST(test_prioritaetsreihenfolge_bei_versuch_0);
    RUN_TEST(test_wiederholung_wird_nie_laenger);
    return UNITY_END();
}
