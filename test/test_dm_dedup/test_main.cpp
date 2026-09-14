// Native Testsuite fuer die zweite Dedup-Ebene auf (Quellrufzeichen, NNN)
// (docs/dm-transport-impl-plan-20260913.md, Stage 2.1).
//
//   pio test -e native -f test_dm_dedup
//
// dmDedupCheck() faengt eine Wiederholung ab, die -- anders als bei der
// msg_id-Dedup (dedup_functions.cpp) -- unter einer FRISCHEN msg_id ankommt:
// Stage 1s Retry-Leiter sendet Versuche 2..9 derselben DM mit neuer msg_id,
// aber gleichbleibender NNN. Aged wird nach Uhrzeit, nicht nach Anzahl --
// node_msgid ist ein einziger 0..999-Zaehler, den ein einzelner Knoten in
// Sekunden durchlaufen kann (src/beacon_rate.h:22-30).

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dm_dedup.h>
#include <crc32_util.h>

void setUp(void)
{
    dmDedupReset();
}

void tearDown(void) {}

static void test_erste_sichtung_ist_new(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000));
}

static void test_gleiches_call_nnn_payload_ist_dup(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1500));
}

// Ein DUP-Treffer darf first_ms NICHT auffrischen -- das Alter zaehlt ab der
// ERSTEN Sichtung. Ein DUP kurz vor Ablauf des Fensters (relativ zu t0)
// aendert daran nichts: bei genau DM_DEDUP_AGE_MS relativ zu t0 ist der
// Eintrag wieder neu, ganz gleich, dass dazwischen ein DUP lag.
static void test_dup_aktualisiert_first_ms_nicht(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000 + DM_DEDUP_AGE_MS - 1));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000 + DM_DEDUP_AGE_MS));
}

// Gleiches (Rufzeichen, NNN), aber anderer Payload: der 0..999-Zaehler ist
// auf eine andere Nachricht gewrappt -- der Eintrag wird ersetzt (NEW), ein
// nachfolgender Treffer mit dem NEUEN Payload ist wieder DUP.
static void test_gleiches_paar_anderer_payload_ist_new_dann_dup(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "servus", 6, 1001));
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 5, "servus", 6, 1002));

    // Der alte Payload ist verdraengt, kein Treffer mehr dagegen.
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1003));
}

// Unterschiedliche Laenge zaehlt schon als "anderer Payload", auch wenn ein
// angehaengtes Zeichen die CRC kaum veraendert.
static void test_andere_laenge_ist_new(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo!", 6, 1001));
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 5, "hallo!", 6, 1002));
}

static void test_anderes_call_gleiche_nnn_ist_new(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("OE1XYZ", 5, "hallo", 5, 1001));

    // beide bleiben unabhaengig als DUP erkennbar
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1002));
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("OE1XYZ", 5, "hallo", 5, 1002));
}

// Rufzeichen mit gemeinsamem Praefix sind verschiedene Paare (strncmp ueber
// den vollen Puffer, nicht nur den Praefix).
static void test_praefix_rufzeichen_sind_verschieden(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-10", 5, "hallo", 5, 1001));

    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 1002));
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-10", 5, "hallo", 5, 1002));
}

// Ein Eintrag, dessen Alter DM_DEDUP_AGE_MS erreicht hat, gilt als
// abwesend -- dieselbe (Rufzeichen, NNN, Payload)-Kombination ist wieder NEW.
static void test_gealterter_eintrag_ist_wieder_new(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, 0));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, DM_DEDUP_AGE_MS));
}

// Der 17. distincte Eintrag (DM_DEDUP_SLOTS==16) verdraengt den mit dem
// hoechsten Alter (den zuerst eingetragenen): dessen Paar ist danach wieder
// NEW, die uebrigen 15 bleiben DUP.
static void test_siebzehnter_eintrag_verdraengt_den_aeltesten(void)
{
    // Einfuegereihenfolge != Altersreihenfolge (Advisor F1): nnn=0 wird nach
    // dem Fuellen mit anderem Payload ersetzt und bekommt damit die JUENGSTE
    // first_ms. Der aelteste Eintrag ist dann nnn=1. Ein Rundlauf-Verdraenger
    // wuerde nnn=0 opfern, ein Alters-Verdraenger nnn=1.
    for(int i = 0; i < DM_DEDUP_SLOTS; i++)
    {
        char payload[8];
        snprintf(payload, sizeof(payload), "p%d", i);
        TEST_ASSERT_EQUAL(DM_DEDUP_NEW,
                           dmDedupCheck("DK5EN-1", (uint16_t)i, payload, strlen(payload), 1000 + (uint32_t)i));
    }
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 0, "q0", 2, 1500));   // ersetzt, jung

    // 17. distinctes Paar, weit innerhalb des Alterungsfensters.
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 16, "p16", 3, 2000));

    // nnn=1 (aeltester) ist verdraengt, nnn=0 (juengster) nicht.
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 0, "q0", 2, 2001));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 1, "p1", 2, 2001));
}

// Laenge unabhaengig von der CRC (Advisor F2): ein laengerer Payload mit
// identischen unteren 16 CRC-Bits muss trotzdem NEW sein. Die Kollision wird
// zur Laufzeit gesucht (3-Byte-Suffix, ~857k Kandidaten, Sekundenbruchteil).
static void test_andere_laenge_bei_gleicher_crc16_ist_new(void)
{
    const char *base = "hallo";
    uint16_t want = (uint16_t)(crc32_buf(base, strlen(base)) & 0xFFFF);
    char cand[16];
    bool found = false;
    for(int a = 0x20; a < 0x7F && !found; a++)
        for(int b = 0x20; b < 0x7F && !found; b++)
            for(int c = 0x20; c < 0x7F && !found; c++)
            {
                snprintf(cand, sizeof(cand), "%s%c%c%c", base, a, b, c);
                if((uint16_t)(crc32_buf(cand, strlen(cand)) & 0xFFFF) == want)
                    found = true;
            }
    TEST_ASSERT_TRUE_MESSAGE(found, "no 3-byte crc16 collision found");

    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, base, strlen(base), 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, cand, strlen(cand), 1001));
}

static void test_millis_rollover(void)
{
    uint32_t near_wrap = 0xFFFFFFF0UL;
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, near_wrap));

    // 32 ms nach dem Wrap: klar innerhalb des Alterungsfensters, bleibt DUP.
    uint32_t wrapped = 0x00000010UL;
    TEST_ASSERT_EQUAL(DM_DEDUP_DUP, dmDedupCheck("DK5EN-1", 5, "hallo", 5, wrapped));

    // DM_DEDUP_AGE_MS ms nach dem urspruenglichen (Vor-Wrap-)Zeitstempel: wieder NEW.
    uint32_t after_window = (uint32_t)(near_wrap + DM_DEDUP_AGE_MS);
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 5, "hallo", 5, after_window));
}

static void test_null_argumente_sind_new_ohne_absturz(void)
{
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck(NULL, 5, "hallo", 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 6, NULL, 5, 1000));
    TEST_ASSERT_EQUAL(DM_DEDUP_NEW, dmDedupCheck("DK5EN-1", 7, NULL, 0, 1000));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_erste_sichtung_ist_new);
    RUN_TEST(test_gleiches_call_nnn_payload_ist_dup);
    RUN_TEST(test_dup_aktualisiert_first_ms_nicht);
    RUN_TEST(test_gleiches_paar_anderer_payload_ist_new_dann_dup);
    RUN_TEST(test_andere_laenge_ist_new);
    RUN_TEST(test_anderes_call_gleiche_nnn_ist_new);
    RUN_TEST(test_praefix_rufzeichen_sind_verschieden);
    RUN_TEST(test_gealterter_eintrag_ist_wieder_new);
    RUN_TEST(test_siebzehnter_eintrag_verdraengt_den_aeltesten);
    RUN_TEST(test_andere_laenge_bei_gleicher_crc16_ist_new);
    RUN_TEST(test_millis_rollover);
    RUN_TEST(test_null_argumente_sind_new_ohne_absturz);
    return UNITY_END();
}
