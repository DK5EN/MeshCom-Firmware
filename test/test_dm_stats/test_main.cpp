// Native Testsuite fuer die DM-Ausgangszaehler und das Send-to-Ack-RTT-
// Histogramm (Stage 0.4, docs/dm-transport-impl-plan-20260913.md).
//
//   pio test -e native -f test_dm_stats
//
// dm_stats.cpp haelt sein 8-Slot-Sendezeit-Tableau (dmstat_sent_tab[]) als
// datei-statischen Zustand ohne oeffentliches Reset -- gewollt: derselbe
// kleine Tisch laeuft auf dem Knoten dauerhaft weiter. Damit trotzdem jeder
// Test mit einem bekannten Tischinhalt beginnt, ohne der Produktionsschnitt-
// stelle einen Test-Reset-Hook hinzuzufuegen: setUp() "primt" den Tisch mit
// acht frischen, garantiert nie zuvor benutzten NNNs. Acht aufeinanderfolgende
// FRISCHE dmStatNoteSent()-Aufrufe schieben den internen Schreibzeiger genau
// einmal durch alle acht Slots -- welcher Wert in welchem physischen Slot
// landet, ist von aussen nie beobachtbar (nur ob eine gegebene NNN noch
// gefunden wird), also macht das Priming den Tischinhalt vollstaendig
// bekannt, unabhaengig davon, was ein vorheriger Test zurueckgelassen hat.
// g_next_base vergibt an jeden Aufrufer (Priming und Testkoerper gleichermassen)
// einen eigenen, nie wiederverwendeten NNN-Bereich, damit sich Priming und
// Testinhalt nie ueberschneiden koennen.

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dm_stats.h>

static uint32_t g_next_base = 100;

static void primeSentTable(uint32_t now_ms)
{
    uint32_t base = g_next_base;
    g_next_base += 8;
    for(int i = 0; i < 8; i++)
        dmStatNoteSent((uint16_t)(base + i), now_ms);
}

void setUp(void)
{
    primeSentTable(1);
}

void tearDown(void) {}

// ---------------------------------------------------------------------------
// dmStatRttBucket() -- reine Funktion, Grenzwerte aus dem Wellenauftrag
// ---------------------------------------------------------------------------

static void test_bucket_grenzwerte(void)
{
    TEST_ASSERT_EQUAL_INT(0, dmStatRttBucket(14999UL));
    TEST_ASSERT_EQUAL_INT(1, dmStatRttBucket(15000UL));

    TEST_ASSERT_EQUAL_INT(1, dmStatRttBucket(39999UL));
    TEST_ASSERT_EQUAL_INT(2, dmStatRttBucket(40000UL));

    TEST_ASSERT_EQUAL_INT(2, dmStatRttBucket(119999UL));
    TEST_ASSERT_EQUAL_INT(3, dmStatRttBucket(120000UL));

    TEST_ASSERT_EQUAL_INT(3, dmStatRttBucket(539999UL));
    TEST_ASSERT_EQUAL_INT(4, dmStatRttBucket(540000UL));

    TEST_ASSERT_EQUAL_INT(4, dmStatRttBucket(1799999UL));
    TEST_ASSERT_EQUAL_INT(5, dmStatRttBucket(1800000UL));
}

// ---------------------------------------------------------------------------
// dmStatNoteSent()/dmStatNoteAck() -- Bucket-Zuordnung und Loeschung nach Ack
// ---------------------------------------------------------------------------

static void test_note_sent_und_ack_buckets_und_loescht_den_eintrag(void)
{
    uint32_t base = g_next_base;
    g_next_base += 8;
    uint16_t nnn = (uint16_t)base;

    dmStatNoteSent(nnn, 1000u);
    dmStatNoteAck(nnn, 1000u + 10000u); // RTT 10000 ms -> Bucket 0

    char buf[256];
    int len = dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "rtt=1/0/0/0/0/0"));

    // dmStatFormat() leert jeden Zaehler (exchange(0)) -- UND den Tabelleneintrag
    // ist bereits durch dmStatNoteAck() geloescht: ein zweiter Ack fuer dieselbe
    // NNN findet nichts mehr und darf keinen weiteren Bucket erhoehen.
    dmStatNoteAck(nnn, 1000u + 20000u);
    len = dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rtt=0/0/0/0/0/0"));
    (void)len;
}

static void test_unbekannte_nnn_wird_ignoriert(void)
{
    uint32_t base = g_next_base;
    g_next_base += 8;
    // Diese NNN wurde nie dmStatNoteSent() uebergeben (Ack fuer eine Nachricht
    // von vor dem Boot, oder eine fremde NNN).
    dmStatNoteAck((uint16_t)base, 12345u);

    char buf[256];
    dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rtt=0/0/0/0/0/0"));
}

static void test_wiederholtes_note_sent_ersetzt_den_alten_eintrag(void)
{
    uint32_t base = g_next_base;
    g_next_base += 8;
    uint16_t nnn = (uint16_t)base;

    // Ein nie bestaetigter Eintrag, dann laeuft der 0..999-Zaehler auf
    // dieselbe NNN: das ist eine neue Nachricht, die alte Zeit ist Muell.
    dmStatNoteSent(nnn, 1000u);
    dmStatNoteSent(nnn, 500000u);

    // 10 s nach der ZWEITEN Zeit -> Bucket 0. Waere die erste geblieben,
    // laege der Ack 509 s danach in Bucket 3.
    dmStatNoteAck(nnn, 500000u + 10000u);

    char buf[256];
    dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rtt=1/0/0/0/0/0"));
}

// ---------------------------------------------------------------------------
// Tabellengroesse: acht Slots, der neunte frische Eintrag verdraengt den
// aeltesten
// ---------------------------------------------------------------------------

static void test_tabelle_ueberschreibt_nach_acht_eintraegen(void)
{
    uint32_t base = g_next_base;
    g_next_base += 9;
    uint16_t ids[9];
    for(int i = 0; i < 9; i++)
        ids[i] = (uint16_t)(base + (uint32_t)i);

    for(int i = 0; i < 8; i++)
        dmStatNoteSent(ids[i], 1000u + (uint32_t)i);

    // Der neunte frische Eintrag verdraengt den physisch aeltesten (ids[0]).
    dmStatNoteSent(ids[8], 9000u);

    char buf[256];

    // ids[0] ist verdraengt: sein Ack findet keinen Eintrag mehr.
    dmStatNoteAck(ids[0], 50000u);
    dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rtt=0/0/0/0/0/0"));

    // ids[1] (noch im Tisch) und ids[8] (der Ersatz) werden beide gefunden.
    dmStatNoteAck(ids[1], 1001u + 5000u); // RTT 5000 -> Bucket 0
    dmStatNoteAck(ids[8], 9000u + 5000u); // RTT 5000 -> Bucket 0
    dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rtt=2/0/0/0/0/0"));
}

// ---------------------------------------------------------------------------
// dmStatFormat() -- Ausgabestring und dass er saemtliche Zaehler zuruecksetzt
// ---------------------------------------------------------------------------

static void test_format_exakter_string_und_reset(void)
{
    dmstat_sent.store(3);
    dmstat_echo.store(1);
    dmstat_gw_ack.store(2);
    dmstat_peer_ack.store(4);
    dmstat_giveup.store(1);
    dmstat_giveup_held.store(6);
    dmstat_attempts.store(9);
    dmstat_reack.store(2);
    dmstat_reack_limited.store(1);
    ringstat_enqueue.store(50);
    ringstat_parked_overwrite.store(5);

    uint32_t base = g_next_base;
    g_next_base += 8;
    dmStatNoteSent((uint16_t)base, 0u);
    dmStatNoteAck((uint16_t)base, 39999u); // RTT 39999 -> Bucket 1

    char buf[256];
    int len = dmStatFormat(buf, sizeof(buf));

    TEST_ASSERT_EQUAL_STRING(
        "DM sent=3 echo=1 gwack=2 ack=4 giveup=1 giveuph=6 att=9 reack=2/1 "
        "rtt=0/1/0/0/0/0 ring=enq:50 ovw:5",
        buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);

    // Zweiter Aufruf ohne neue Ereignisse: jeder Zaehler wieder auf 0 --
    // dmStatFormat() hat sie per exchange(0) geleert.
    len = dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING(
        "DM sent=0 echo=0 gwack=0 ack=0 giveup=0 giveuph=0 att=0 reack=0/0 "
        "rtt=0/0/0/0/0/0 ring=enq:0 ovw:0",
        buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);
}

// ---------------------------------------------------------------------------
// Klemmung bei zu kleinem Puffer / ungueltigen Argumenten
// ---------------------------------------------------------------------------

static void test_klemmung_bei_zu_kleinem_puffer(void)
{
    // n == 0: Puffer bleibt unangetastet, Rueckgabe 0.
    char guard = 0x7F;
    TEST_ASSERT_EQUAL_INT(0, dmStatFormat(&guard, 0));
    TEST_ASSERT_EQUAL_INT(0x7F, (int)guard);

    // buf == NULL ist ebenso abgesichert.
    TEST_ASSERT_EQUAL_INT(0, dmStatFormat(NULL, 100));

    // n == 1: nur Platz fuer die NUL -- leerer String, Rueckgabe 0.
    char eng[2] = {0x7F, 0x7F};
    TEST_ASSERT_EQUAL_INT(0, dmStatFormat(eng, 1));
    TEST_ASSERT_EQUAL_STRING("", eng);

    // n kleiner als die volle Zeile: Rueckgabe ist n-1 (nicht die
    // Wunschlaenge von snprintf), der Puffer bei n-1 sauber terminiert.
    char klein[6];
    memset(klein, 0x7F, sizeof(klein));
    int len = dmStatFormat(klein, sizeof(klein));
    TEST_ASSERT_EQUAL_INT((int)sizeof(klein) - 1, len);
    TEST_ASSERT_EQUAL_INT((int)strlen(klein), len);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_bucket_grenzwerte);
    RUN_TEST(test_note_sent_und_ack_buckets_und_loescht_den_eintrag);
    RUN_TEST(test_unbekannte_nnn_wird_ignoriert);
    RUN_TEST(test_wiederholtes_note_sent_ersetzt_den_alten_eintrag);
    RUN_TEST(test_tabelle_ueberschreibt_nach_acht_eintraegen);
    RUN_TEST(test_format_exakter_string_und_reset);
    RUN_TEST(test_klemmung_bei_zu_kleinem_puffer);
    return UNITY_END();
}
