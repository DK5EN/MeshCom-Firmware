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
#include <fstream>
#include <sstream>
#include <string>

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

// ---- Brief G: Alters-Boden ----
static void test_age_stage_grenzen(void)
{
    TEST_ASSERT_EQUAL_INT(0, csmaAgeStage(0));
    TEST_ASSERT_EQUAL_INT(0, csmaAgeStage(CSMA_AGE_STAGE1_MS - 1));
    TEST_ASSERT_EQUAL_INT(1, csmaAgeStage(CSMA_AGE_STAGE1_MS));
    TEST_ASSERT_EQUAL_INT(1, csmaAgeStage(CSMA_AGE_STAGE2_MS - 1));
    TEST_ASSERT_EQUAL_INT(2, csmaAgeStage(CSMA_AGE_STAGE2_MS));
    TEST_ASSERT_EQUAL_INT(2, csmaAgeStage(1000000UL));
}

static void test_age_stage2_null_heisst_nie(void)
{
    TEST_ASSERT_EQUAL_INT(1, csmaAgeStageEx(1000000UL, 20000UL, 0UL));
    TEST_ASSERT_EQUAL_INT(0, csmaAgeStageEx(19999UL, 20000UL, 0UL));
    TEST_ASSERT_EQUAL_INT(2, csmaAgeStageEx(60000UL, 20000UL, 60000UL));
}

static void test_age_stage1_clamp(void)
{
    TEST_ASSERT_EQUAL_UINT32(1500, csmaAgedHoldMs(1, 710, 0));  // 1965 -> Max
    TEST_ASSERT_EQUAL_UINT32(1425, csmaAgedHoldMs(1, 1250, 0)); // 2850-1250-175
    TEST_ASSERT_EQUAL_UINT32(500, csmaAgedHoldMs(1, 5000, 0));  // Min
    TEST_ASSERT_EQUAL_UINT32(1425 + 5 * CSMA_SLOT_SIZE, csmaAgedHoldMs(1, 1250, 5));
}

static void test_age_stage2_wert(void)
{
    TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaAgedHoldMs(2, 710, 0));
    TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS + 3 * CSMA_SLOT_SIZE, csmaAgedHoldMs(2, 710, 3));
}

static void test_age_floor_min_mit_case_a(void)
{
    // heutiges Ergebnis 150 ms (Fall A) bleibt bei Stufe 1 (Floor 1500 > 150); bei Stufe 2 greift min() mit 100 ms
    TEST_ASSERT_EQUAL_UINT32(100, csmaApplyAgeFloor(150, 70000UL, MSG_PRIO_NORMAL, 710, 0)); // Stufe 2 (100) < 150
    TEST_ASSERT_EQUAL_UINT32(150, csmaApplyAgeFloor(150, 25000UL, MSG_PRIO_NORMAL, 710, 0)); // Stufe 1 (1500): Fall A bleibt
    // heutiges Ergebnis 4500 -> Stufe 1: 1500
    TEST_ASSERT_EQUAL_UINT32(1500, csmaApplyAgeFloor(4500, 25000UL, MSG_PRIO_NORMAL, 710, 0));
    // Stufe 0: unveraendert
    TEST_ASSERT_EQUAL_UINT32(4500, csmaApplyAgeFloor(4500, 5000UL, MSG_PRIO_NORMAL, 710, 0));
    // Stufe 2
    TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaApplyAgeFloor(4500, 61000UL, MSG_PRIO_CRITICAL, 710, 0));
}

static void test_age_floor_prio_4_5_ausgeschlossen(void)
{
    TEST_ASSERT_EQUAL_UINT32(5500, csmaApplyAgeFloor(5500, 90000UL, MSG_PRIO_LOW, 710, 0));
    TEST_ASSERT_EQUAL_UINT32(5500, csmaApplyAgeFloor(5500, 90000UL, MSG_PRIO_BACKGROUND, 710, 0));
    TEST_ASSERT_EQUAL_UINT32(1500, csmaApplyAgeFloor(5500, 30000UL, MSG_PRIO_HIGH, 710, 0));
}

// ---- CSMA-01: re-arm a running wait, shorten only ----
static unsigned long rearmFor(bool armed, uint8_t armed_prio, unsigned long armed_ms, uint8_t new_prio, long jit)
{
    if (!csmaShouldRearm(armed, armed_prio, new_prio)) return armed_ms;
    return csmaRearm(armed_ms, csmaTimeoutPrio(0, new_prio, jit));
}

void test_csma01_hoehere_prio_verkuerzt_laufende_wartezeit()
{
    unsigned long relay = csmaTimeoutPrio(0, MSG_PRIO_NORMAL, 3);   // armed for a relay at the head
    unsigned long cand  = csmaTimeoutPrio(0, MSG_PRIO_CRITICAL, 3);
    TEST_ASSERT_TRUE(cand < relay);
    TEST_ASSERT_EQUAL_UINT32(cand, rearmFor(true, MSG_PRIO_NORMAL, relay, MSG_PRIO_CRITICAL, 3));
}

void test_csma01_gleiche_oder_niedrigere_prio_aendert_nichts()
{
    unsigned long relay = csmaTimeoutPrio(0, MSG_PRIO_NORMAL, 2);
    TEST_ASSERT_EQUAL_UINT32(relay, rearmFor(true, MSG_PRIO_NORMAL, relay, MSG_PRIO_NORMAL, 0));
    TEST_ASSERT_EQUAL_UINT32(relay, rearmFor(true, MSG_PRIO_NORMAL, relay, MSG_PRIO_LOW, 0));
    TEST_ASSERT_EQUAL_UINT32(relay, rearmFor(true, MSG_PRIO_NORMAL, relay, MSG_PRIO_BACKGROUND, 0));
    TEST_ASSERT_FALSE(csmaShouldRearm(true, MSG_PRIO_CRITICAL, MSG_PRIO_HIGH));
}

void test_csma01_verlaengert_nie()
{
    // armed wait already shorter than the candidate (e.g. age floor): stays
    TEST_ASSERT_EQUAL_UINT32(1500, csmaRearm(1500, 3105));
    TEST_ASSERT_EQUAL_UINT32(1500, csmaRearm(1500, 1500));
    TEST_ASSERT_EQUAL_UINT32(1500, rearmFor(true, MSG_PRIO_NORMAL, 1500, MSG_PRIO_CRITICAL, 3));
}

void test_csma01_nicht_scharf_keine_aenderung()
{
    TEST_ASSERT_FALSE(csmaShouldRearm(false, MSG_PRIO_NORMAL, MSG_PRIO_CRITICAL));
    TEST_ASSERT_EQUAL_UINT32(4600, rearmFor(false, MSG_PRIO_NORMAL, 4600, MSG_PRIO_CRITICAL, 0));
}


// ---- CSMA-05: idle re-anchor ceiling ----
void test_csma05_idle_ceiling_deckt_jede_klasse_ab()
{
    for (int attempt = 0; attempt < CSMA_MAX_ATTEMPTS; attempt++) {
        unsigned long c = csmaIdleCeilingMs(attempt);
        for (uint8_t p = MSG_PRIO_CRITICAL; p <= MSG_PRIO_BACKGROUND; p++) {
            for (long j = 0; j <= csmaSlotsForPrio(p); j++)
                TEST_ASSERT_TRUE(csmaTimeoutPrio(attempt, p, j) <= c);
        }
    }
    TEST_ASSERT_EQUAL_UINT32(CSMA_RAPID_RX_MS, csmaIdleCeilingMs(CSMA_MAX_ATTEMPTS));
}

// After an idle re-anchor (prio 0xFF, wait = ceiling) a low-priority frame must NOT inherit the short wait
// a CRITICAL frame had armed before, and a CRITICAL frame still gets its own short wait.
void test_csma05_nach_idle_erbt_niedrige_prio_keine_kurze_wartezeit()
{
    unsigned long before_short = csmaTimeoutPrio(0, MSG_PRIO_CRITICAL, 0);   // stale short wait
    unsigned long idle = csmaIdleCeilingMs(0);
    TEST_ASSERT_TRUE(idle > before_short);
    TEST_ASSERT_TRUE(csmaShouldRearm(true, 0xFF, MSG_PRIO_BACKGROUND));
    unsigned long low = csmaRearm(idle, csmaTimeoutPrio(0, MSG_PRIO_BACKGROUND, 4));
    TEST_ASSERT_TRUE(low > before_short + 1000);
    unsigned long hi = csmaRearm(idle, csmaTimeoutPrio(0, MSG_PRIO_CRITICAL, 4));
    TEST_ASSERT_EQUAL_UINT32(csmaTimeoutPrio(0, MSG_PRIO_CRITICAL, 4), hi);
}

// ---- CSMA-03: stale CAD-free result -> one fresh CAD ----
void test_csma03_frisches_ergebnis_sendet_veraltetes_wird_neu_gescannt()
{
    TEST_ASSERT_FALSE(csmaCadFreeNeedsRecheck(csmaCadResultAgeUs(1000, 1000 + CSMA_CAD_MAX_AGE_US), false));
    TEST_ASSERT_TRUE(csmaCadFreeNeedsRecheck(csmaCadResultAgeUs(1000, 1000 + CSMA_CAD_MAX_AGE_US + 1), false));
    // bounded: the one fresh CAD is spent -> transmit even if the loop is slow
    TEST_ASSERT_FALSE(csmaCadFreeNeedsRecheck(1000000UL, true));
}

void test_csma03_alter_ist_micros_wrap_sicher()
{
    TEST_ASSERT_EQUAL_UINT32(300, csmaCadResultAgeUs(0xFFFFFF00UL, 0x0000002CUL));
    TEST_ASSERT_FALSE(csmaCadFreeNeedsRecheck(csmaCadResultAgeUs(0xFFFFFF00UL, 0x0000002CUL), false));
}

static void test_csma03_loop_delay_1ms_waehrend_cad(void)
{
    TEST_ASSERT_EQUAL_UINT32(100, csmaLoopDelayMs(false, false));
    TEST_ASSERT_EQUAL_UINT32(1, csmaLoopDelayMs(true, false));
    TEST_ASSERT_EQUAL_UINT32(1, csmaLoopDelayMs(false, true));
    TEST_ASSERT_EQUAL_UINT32(1, csmaLoopDelayMs(true, true));
}

static void test_csma03_nrf52loop_delay_nutzt_helper(void)
{
    std::string file(__FILE__);
    const std::string suffix = "test/test_csma_timing/test_csma_timing.cpp";
    std::string path = file.substr(0, file.size() - suffix.size()) + "src/nrf52/nrf52_main.cpp";
    std::ifstream f(path.c_str(), std::ios::binary);
    TEST_ASSERT_TRUE(f.good());
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    TEST_ASSERT_TRUE(s.find("delay(csmaLoopDelayMs(") != std::string::npos);
    TEST_ASSERT_TRUE(s.find("    delay(100);\n\n    yield();") == std::string::npos);
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
    RUN_TEST(test_age_stage_grenzen);
    RUN_TEST(test_age_stage2_null_heisst_nie);
    RUN_TEST(test_age_stage1_clamp);
    RUN_TEST(test_age_stage2_wert);
    RUN_TEST(test_age_floor_min_mit_case_a);
    RUN_TEST(test_age_floor_prio_4_5_ausgeschlossen);
    RUN_TEST(test_csma01_hoehere_prio_verkuerzt_laufende_wartezeit);
    RUN_TEST(test_csma01_gleiche_oder_niedrigere_prio_aendert_nichts);
    RUN_TEST(test_csma01_verlaengert_nie);
    RUN_TEST(test_csma01_nicht_scharf_keine_aenderung);
    RUN_TEST(test_csma05_idle_ceiling_deckt_jede_klasse_ab);
    RUN_TEST(test_csma03_frisches_ergebnis_sendet_veraltetes_wird_neu_gescannt);
    RUN_TEST(test_csma03_alter_ist_micros_wrap_sicher);
    RUN_TEST(test_csma03_loop_delay_1ms_waehrend_cad);
    RUN_TEST(test_csma03_nrf52loop_delay_nutzt_helper);
    RUN_TEST(test_csma05_nach_idle_erbt_niedrige_prio_keine_kurze_wartezeit);
    return UNITY_END();
}
