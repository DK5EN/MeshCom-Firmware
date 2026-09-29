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

#include "../support/teleport_clock.h"

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

// F1: dmStatNoteAck() meldet, ob DIESER Ack der erste fuer eine notierte NNN
// war. Die Aufrufer (LoRa- und Server/UDP-Pfad) haengen dmstat_peer_ack daran,
// damit ack= und rtt= je eigener DM genau einmal zaehlen.
static void test_note_ack_meldet_nur_den_ersten_ack_je_nnn(void)
{
    uint32_t base = g_next_base;
    g_next_base += 8;
    uint16_t nnn = (uint16_t)base;

    dmStatNoteSent(nnn, 1000u);
    TEST_ASSERT_TRUE(dmStatNoteAck(nnn, 1000u + 5000u));   // erster Ack: Eintrag gefunden und geleert
    TEST_ASSERT_FALSE(dmStatNoteAck(nnn, 1000u + 9000u));  // zweiter Ack (anderer Pfad/Dublette)
    TEST_ASSERT_FALSE(dmStatNoteAck(nnn, 1000u + 20000u)); // und jeder weitere

    // RTT wurde nur beim ersten Ack gebucht (5 s -> Bucket 0), nicht erneut.
    char buf[256];
    dmStatFormat(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "rtt=1/0/0/0/0/0"));
}

static void test_note_ack_unbekannte_nnn_liefert_false(void)
{
    uint32_t base = g_next_base;
    g_next_base += 8;
    TEST_ASSERT_FALSE(dmStatNoteAck((uint16_t)base, 12345u));

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

// ---------------------------------------------------------------------------
// millis() teleportation (test/support/teleport_clock.h): RTT is `now_ms -
// sent_ms` in uint32_t, so a DM sent before the wrap and acked after it must
// still get its true delta, whatever the size of the gap.
// ---------------------------------------------------------------------------

static void rttClear(void)
{
    for(int i = 0; i < DMSTAT_RTT_BUCKETS; i++)
        dmstat_rtt[i].store(0);
}

// Exactly one count, in exactly bucket `want`, nowhere else.
static void assertOnlyBucket(int want, const char *msg)
{
    for(int i = 0; i < DMSTAT_RTT_BUCKETS; i++)
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(i == want ? 1u : 0u, dmstat_rtt[i].load(), msg);
}

// Sent k ms before the wrap (k = 0 .. right on it), acked rtt ms later (after
// the wrap for every rtt > k): the bucket is the one of the TRUE rtt, with
// the exact bucket edges.
static void test_teleport_rtt_across_wrap_is_true_delta(void)
{
    const uint32_t rtts[] = {0, 1, 14999, 15000, 39999, 40000, 119999, 120000,
                             539999, 540000, 1799999, 1800000, 0x7FFFFFFFu, 0x80000001u, 0xFFFFFFFFu};
    const uint32_t ks[] = {0, 1, 999, 14999, 20000, 600000};
    for(unsigned ki = 0; ki < sizeof(ks) / sizeof(ks[0]); ki++)
    {
        for(unsigned ri = 0; ri < sizeof(rtts) / sizeof(rtts[0]); ri++)
        {
            rttClear();
            uint32_t base = g_next_base;
            g_next_base += 8;
            uint16_t nnn = (uint16_t)(base % 1000);

            TeleportClock c;
            c.teleport_to(TeleportClock::kWrapMinus(ks[ki]));
            dmStatNoteSent(nnn, c.now_ms);
            c.advance(rtts[ri]);

            char msg[64];
            snprintf(msg, sizeof(msg), "k=%u rtt=%u", (unsigned)ks[ki], (unsigned)rtts[ri]);
            TEST_ASSERT_TRUE_MESSAGE(dmStatNoteAck(nnn, c.now_ms), msg);
            assertOnlyBucket(dmStatRttBucket(rtts[ri]), msg);
        }
    }
}

// A stale entry, far older than the 30 min histogram window: the ack still
// finds it (nothing expires by time), books the true delta into the top
// bucket, and once (a second ack finds nothing).
static void test_teleport_stale_entry_is_booked_in_top_bucket_once(void)
{
    const uint32_t ages[] = {1800000u, 86400000u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u,
                             2592000000u /* 30 d */, 0xFFFFFFFFu /* just under 49.7 d */};
    for(unsigned i = 0; i < sizeof(ages) / sizeof(ages[0]); i++)
    {
        rttClear();
        uint32_t base = g_next_base;
        g_next_base += 8;
        uint16_t nnn = (uint16_t)(base % 1000);

        TeleportClock c;
        c.teleport_to(TeleportClock::kWrapMinus(40000));   // sent just before the wrap
        dmStatNoteSent(nnn, c.now_ms);
        c.advance(ages[i]);                                  // the node idles, then the ack arrives

        char msg[48];
        snprintf(msg, sizeof(msg), "age=%u", (unsigned)ages[i]);
        TEST_ASSERT_TRUE_MESSAGE(dmStatNoteAck(nnn, c.now_ms), msg);
        assertOnlyBucket(5, msg);
        TEST_ASSERT_FALSE_MESSAGE(dmStatNoteAck(nnn, c.now_ms + 10), msg);
        assertOnlyBucket(5, msg);
    }
}

// NNN counter wrapped onto a never-acked entry from before the millis() wrap:
// the re-note replaces the send time, the RTT counts from the NEW send.
static void test_teleport_renote_after_wrap_replaces_send_time(void)
{
    rttClear();
    uint32_t base = g_next_base;
    g_next_base += 8;
    uint16_t nnn = (uint16_t)(base % 1000);

    TeleportClock c;
    c.teleport_to(TeleportClock::kWrapMinus(19999));
    dmStatNoteSent(nnn, c.now_ms);       // old send, 20 s before the wrap
    c.advance(21000);                    // now 1 s after the wrap
    dmStatNoteSent(nnn, c.now_ms);       // same NNN again: replaces
    c.advance(4000);
    TEST_ASSERT_TRUE(dmStatNoteAck(nnn, c.now_ms));
    assertOnlyBucket(0, "rtt from the re-send (4 s), not from the old send (25 s)");
}

// Table slots keep working across the wrap: a burst of DMs straddling it
// (older ones overwritten in ring order) each get their own true RTT.
static void test_teleport_table_burst_straddling_wrap(void)
{
    rttClear();
    uint32_t base = g_next_base;
    g_next_base += 8;

    TeleportClock c;
    c.teleport_to(TeleportClock::kWrapMinus(3500));
    uint32_t sentAt[8];
    for(int i = 0; i < 8; i++)
    {
        sentAt[i] = c.now_ms;
        dmStatNoteSent((uint16_t)((base + i) % 1000), c.now_ms);
        c.advance(1000);                 // DM 4 lands after the wrap
    }
    // ack them all 16 s after the last send: rtt = 16 s + (7 - i) s -> buckets 1 (<40 s)
    c.advance(16000);
    for(int i = 0; i < 8; i++)
        TEST_ASSERT_TRUE(dmStatNoteAck((uint16_t)((base + i) % 1000), c.now_ms));
    for(int i = 0; i < DMSTAT_RTT_BUCKETS; i++)
        TEST_ASSERT_EQUAL_UINT32(i == 1 ? 8u : 0u, dmstat_rtt[i].load());
    (void)sentAt;
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_bucket_grenzwerte);
    RUN_TEST(test_note_sent_und_ack_buckets_und_loescht_den_eintrag);
    RUN_TEST(test_unbekannte_nnn_wird_ignoriert);
    RUN_TEST(test_note_ack_meldet_nur_den_ersten_ack_je_nnn);
    RUN_TEST(test_note_ack_unbekannte_nnn_liefert_false);
    RUN_TEST(test_wiederholtes_note_sent_ersetzt_den_alten_eintrag);
    RUN_TEST(test_tabelle_ueberschreibt_nach_acht_eintraegen);
    RUN_TEST(test_format_exakter_string_und_reset);
    RUN_TEST(test_klemmung_bei_zu_kleinem_puffer);
    RUN_TEST(test_teleport_rtt_across_wrap_is_true_delta);
    RUN_TEST(test_teleport_stale_entry_is_booked_in_top_bucket_once);
    RUN_TEST(test_teleport_renote_after_wrap_replaces_send_time);
    RUN_TEST(test_teleport_table_burst_straddling_wrap);
    return UNITY_END();
}
