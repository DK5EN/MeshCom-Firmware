// Host-Test fuer src/byte_fifo.h -- der Byte-Ring, der die drei
// Schlitzringe (Telefon-Daten, Telefon-Kommandos, UDP-Ausgang) ersetzt.
//
//   pio test -e native_byte_fifo -f test_byte_fifo
//
// Die Faelle sind die, in denen ein Byte-Ring anders scheitern kann als ein
// Schlitzring: Umbruch eines Frames am Speicherende, Verdraengung ueber die
// Lesestelle hinweg (was addRingPointer() frueher stumm tat), der Verlauf
// nach dem Lesen, und die Generationszaehler, an denen der UDP-Drain
// erkennt, dass sein Frame waehrend des Sendens verdraengt wurde.

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <byte_fifo.h>

static uint8_t store[64];
static byte_fifo_t f = BYTE_FIFO_INIT(store);

void setUp(void)
{
    memset(store, 0xEE, sizeof(store));
    bf_reset(&f);
}

void tearDown(void) {}

static void fill(uint8_t *b, uint8_t len, uint8_t seed)
{
    for (int i = 0; i < len; i++)
        b[i] = (uint8_t)(seed + i);
}

static void expect_frame(uint8_t *got, uint8_t len, uint8_t seed, const char *what)
{
    uint8_t want[256];
    fill(want, len, seed);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(want, got, len, what);
}

// --- Grundlauf ---------------------------------------------------------------

static void test_empty_ring(void)
{
    uint8_t out[8];
    TEST_ASSERT_TRUE(bf_empty(&f));
    TEST_ASSERT_EQUAL_UINT8(0, bf_peek(&f, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT16(0, bf_frames(&f));
    bf_pop(&f); // darf nichts tun
    TEST_ASSERT_EQUAL_UINT16(0, bf_unread(&f));
}

static void test_push_peek_pop_in_order(void)
{
    uint8_t a[10], b[7], out[32];
    fill(a, 10, 0x10);
    fill(b, 7, 0x40);
    TEST_ASSERT_EQUAL_INT(0, bf_push(&f, a, 10));
    TEST_ASSERT_EQUAL_INT(0, bf_push(&f, b, 7));
    TEST_ASSERT_EQUAL_UINT16(2, bf_unread(&f));
    TEST_ASSERT_EQUAL_UINT16(19, bf_used(&f));

    TEST_ASSERT_EQUAL_UINT8(10, bf_peek(&f, out, sizeof(out)));
    expect_frame(out, 10, 0x10, "first frame");
    bf_pop(&f);
    TEST_ASSERT_EQUAL_UINT8(7, bf_peek(&f, out, sizeof(out)));
    expect_frame(out, 7, 0x40, "second frame");
    bf_pop(&f);
    TEST_ASSERT_TRUE(bf_empty(&f));
    // Verlauf bleibt liegen
    TEST_ASSERT_EQUAL_UINT16(2, bf_frames(&f));
}

static void test_push2_concatenates_two_parts(void)
{
    // Der Telefon-Ring haengt an jeden Frame 4 Byte Zeitstempel an.
    uint8_t a[5] = {1, 2, 3, 4, 5}, t[4] = {0xAA, 0xBB, 0xCC, 0xDD}, out[16];
    TEST_ASSERT_EQUAL_INT(0, bf_push2(&f, a, 5, t, 4));
    TEST_ASSERT_EQUAL_UINT8(9, bf_peek(&f, out, sizeof(out)));
    uint8_t want[9] = {1, 2, 3, 4, 5, 0xAA, 0xBB, 0xCC, 0xDD};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 9);
}

static void test_peek_truncates_but_reports_full_length(void)
{
    uint8_t a[20], out[8];
    fill(a, 20, 0x30);
    bf_push(&f, a, 20);
    memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_UINT8(20, bf_peek(&f, out, 8));
    expect_frame(out, 8, 0x30, "first 8 bytes");
}

static void test_rejects_zero_and_oversize(void)
{
    uint8_t a[64];
    TEST_ASSERT_EQUAL_INT(-1, bf_push(&f, a, 0));
    TEST_ASSERT_EQUAL_INT(-1, bf_push(&f, a, 64)); // 64+1 > cap 64
    TEST_ASSERT_EQUAL_INT(0, bf_push(&f, a, 63));  // 63+1 == cap: passt genau
    TEST_ASSERT_EQUAL_UINT16(64, bf_used(&f));
    TEST_ASSERT_EQUAL_INT(-1, bf_push2(&f, a, 200, a, 100)); // 300 > 255
}

// --- Umbruch -----------------------------------------------------------------

static void test_frame_wraps_around_the_end(void)
{
    // 3 x 20 Byte = 63 belegt. Das vierte muss das erste verdraengen und
    // bricht dabei bei Byte 63 um.
    uint8_t a[20], out[32];
    for (int i = 0; i < 3; i++)
    {
        fill(a, 20, (uint8_t)(i * 0x20));
        TEST_ASSERT_EQUAL_INT(0, bf_push(&f, a, 20));
    }
    bf_pop(&f); // erstes gelesen -> Verdraengung kostet keinen ungelesenen
    fill(a, 20, 0x60);
    TEST_ASSERT_EQUAL_INT(0, bf_push(&f, a, 20));
    TEST_ASSERT_EQUAL_UINT16(3, bf_frames(&f));
    TEST_ASSERT_EQUAL_UINT16(3, bf_unread(&f));

    TEST_ASSERT_EQUAL_UINT8(20, bf_peek(&f, out, sizeof(out)));
    expect_frame(out, 20, 0x20, "second survives");
    bf_pop(&f);
    bf_pop(&f);
    TEST_ASSERT_EQUAL_UINT8(20, bf_peek(&f, out, sizeof(out)));
    expect_frame(out, 20, 0x60, "wrapped frame intact");
}

static void test_many_wraps_keep_order(void)
{
    // Lange Folge mit wechselnden Laengen; jeder Frame muss so zurueckkommen,
    // wie er hineinging, in Reihenfolge, ueber viele Umbrueche.
    uint8_t a[40], out[40];
    uint8_t next_in = 0, next_out = 0;
    for (int round = 0; round < 500; round++)
    {
        uint8_t len = (uint8_t)(1 + (round * 7) % 30);
        fill(a, len, next_in);
        int lost = bf_push(&f, a, len);
        TEST_ASSERT_TRUE_MESSAGE(lost >= 0, "push rejected");
        next_out = (uint8_t)(next_out + lost); // verdraengte ungelesene ueberspringen
        next_in++;
        if (round % 3 == 0)
        {
            uint8_t l = bf_peek(&f, out, sizeof(out));
            TEST_ASSERT_NOT_EQUAL(0, l);
            char msg[48];
            snprintf(msg, sizeof(msg), "round %d frame %u", round, next_out);
            expect_frame(out, l, next_out, msg);
            bf_pop(&f);
            next_out++;
        }
    }
}

// --- Verdraengung ------------------------------------------------------------

static void test_eviction_moves_tail_and_counts_unread(void)
{
    uint8_t a[30], out[32];
    fill(a, 30, 0x00);
    bf_push(&f, a, 30);           // 31 belegt
    fill(a, 30, 0x40);
    bf_push(&f, a, 30);           // 62 belegt, beide ungelesen
    fill(a, 10, 0x80);
    int lost = bf_push(&f, a, 10); // braucht 11, frei sind 2 -> erster fliegt
    TEST_ASSERT_EQUAL_INT(1, lost);
    TEST_ASSERT_EQUAL_UINT16(2, bf_unread(&f));
    TEST_ASSERT_EQUAL_UINT8(30, bf_peek(&f, out, sizeof(out)));
    expect_frame(out, 30, 0x40, "tail moved to the survivor");
}

static void test_eviction_of_read_history_is_free(void)
{
    uint8_t a[30];
    fill(a, 30, 0x00);
    bf_push(&f, a, 30);
    bf_push(&f, a, 30);
    bf_pop(&f);
    bf_pop(&f);
    TEST_ASSERT_EQUAL_UINT16(2, bf_frames(&f));
    int lost = bf_push(&f, a, 30); // verdraengt einen GELESENEN Frame
    TEST_ASSERT_EQUAL_INT(0, lost);
    TEST_ASSERT_EQUAL_UINT16(1, bf_unread(&f));
    TEST_ASSERT_EQUAL_UINT16(2, bf_frames(&f));
}

static void test_tail_gen_detects_eviction_during_send(void)
{
    // Das Muster des UDP-Drains: peek, etwas Langsames tun, dann nur pop(),
    // wenn niemand den Frame inzwischen verdraengt hat.
    uint8_t a[30], out[32];
    fill(a, 30, 0x00);
    bf_push(&f, a, 30);
    uint16_t g = bf_tail_gen(&f);
    bf_peek(&f, out, sizeof(out));
    // Schreiber ueberholt: zwei weitere 30er verdraengen den ersten.
    bf_push(&f, a, 30);
    bf_push(&f, a, 30);
    TEST_ASSERT_NOT_EQUAL(g, bf_tail_gen(&f));
    // Ohne Verdraengung bleibt die Generation stehen.
    bf_reset(&f);
    bf_push(&f, a, 30);
    g = bf_tail_gen(&f);
    bf_peek(&f, out, sizeof(out));
    bf_push(&f, a, 20);
    TEST_ASSERT_EQUAL_UINT16(g, bf_tail_gen(&f));
    bf_pop(&f);
    TEST_ASSERT_NOT_EQUAL(g, bf_tail_gen(&f));
}

// --- Verlauf -----------------------------------------------------------------

static void test_history_iterates_oldest_to_newest_including_read(void)
{
    uint8_t a[8], out[16];
    for (int i = 0; i < 4; i++)
    {
        fill(a, 8, (uint8_t)(i * 0x10));
        bf_push(&f, a, 8);
    }
    bf_pop(&f);
    bf_pop(&f); // zwei gelesen, zwei ungelesen -- der Verlauf hat alle vier
    bf_iter_t it;
    bf_iter_begin(&f, &it);
    for (int i = 0; i < 4; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(8, bf_iter_next(&f, &it, out, sizeof(out)));
        char msg[32];
        snprintf(msg, sizeof(msg), "history %d", i);
        expect_frame(out, 8, (uint8_t)(i * 0x10), msg);
    }
    TEST_ASSERT_EQUAL_UINT8(0, bf_iter_next(&f, &it, out, sizeof(out)));
}

static void test_history_stops_after_eviction_mid_walk(void)
{
    uint8_t a[30], out[32];
    fill(a, 30, 0x00);
    bf_push(&f, a, 30);
    bf_push(&f, a, 30);
    bf_iter_t it;
    bf_iter_begin(&f, &it);
    TEST_ASSERT_EQUAL_UINT8(30, bf_iter_next(&f, &it, out, sizeof(out)));
    bf_push(&f, a, 30); // verdraengt den ersten, verschiebt oldest
    TEST_ASSERT_EQUAL_UINT8(0, bf_iter_next(&f, &it, out, sizeof(out)));
}

static void test_reset_clears_everything(void)
{
    uint8_t a[8];
    bf_push(&f, a, 8);
    bf_push(&f, a, 8);
    bf_reset(&f);
    TEST_ASSERT_TRUE(bf_empty(&f));
    TEST_ASSERT_EQUAL_UINT16(0, bf_frames(&f));
    TEST_ASSERT_EQUAL_UINT16(0, bf_used(&f));
}


// bf_unread()/bf_frames() zaehlen FRAMES, bf_used() zaehlt BYTES. Der
// Unterschied hat einmal Geld gekostet: eine Drossel verglich bf_unread()
// direkt mit der Ringkapazitaet in Byte, war damit still wirkungslos und
// liess sendMheard() die gerade selbst geschriebenen Frames verdraengen.
// Dieser Fall nagelt die Einheiten fest, damit der naechste Leser sie nicht
// wieder verwechselt.
static void test_unread_counts_frames_used_counts_bytes(void)
{
    uint8_t a[20];
    memset(a, 'x', sizeof(a));

    bf_push(&f, a, (uint8_t)sizeof(a));

    // EIN Frame -- nicht 20, nicht 21.
    TEST_ASSERT_EQUAL_UINT16(1, bf_unread(&f));
    TEST_ASSERT_EQUAL_UINT16(1, bf_frames(&f));
    // Bytes dagegen: Nutzlast plus Laengenbyte.
    TEST_ASSERT_EQUAL_UINT16(21, bf_used(&f));

    bf_push(&f, a, (uint8_t)sizeof(a));
    TEST_ASSERT_EQUAL_UINT16(2, bf_unread(&f));
    TEST_ASSERT_EQUAL_UINT16(42, bf_used(&f));

    // bf_pop() senkt nur die ungelesenen Frames; der Verlauf und damit
    // bf_used() bleibt stehen. Wer bf_used() als Drossel nimmt, haengt nach
    // dem ersten vollen Ringumlauf dauerhaft.
    bf_pop(&f);
    TEST_ASSERT_EQUAL_UINT16(1, bf_unread(&f));
    TEST_ASSERT_EQUAL_UINT16(2, bf_frames(&f));
    TEST_ASSERT_EQUAL_UINT16(42, bf_used(&f));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_ring);
    RUN_TEST(test_push_peek_pop_in_order);
    RUN_TEST(test_push2_concatenates_two_parts);
    RUN_TEST(test_peek_truncates_but_reports_full_length);
    RUN_TEST(test_rejects_zero_and_oversize);
    RUN_TEST(test_frame_wraps_around_the_end);
    RUN_TEST(test_many_wraps_keep_order);
    RUN_TEST(test_eviction_moves_tail_and_counts_unread);
    RUN_TEST(test_eviction_of_read_history_is_free);
    RUN_TEST(test_tail_gen_detects_eviction_during_send);
    RUN_TEST(test_history_iterates_oldest_to_newest_including_read);
    RUN_TEST(test_history_stops_after_eviction_mid_walk);
    RUN_TEST(test_unread_counts_frames_used_counts_bytes);
    RUN_TEST(test_reset_clears_everything);
    return UNITY_END();
}
