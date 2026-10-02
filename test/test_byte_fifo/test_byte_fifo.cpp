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

// BF_TEST_LOCK_HOOK (env:native_byte_fifo): byte_fifo.cpp ruft diese statt
// der nRF52-Sperre. depth muss nach jedem Aufruf wieder 0 sein.
static int lock_calls = 0;
static int lock_depth = 0;
void bf_test_lock(void) { lock_calls++; lock_depth++; }
void bf_test_unlock(void) { lock_depth--; }

static uint8_t store[64];
static byte_fifo_t f = BYTE_FIFO_INIT(store);

void setUp(void)
{
    lock_calls = 0;
    lock_depth = 0;
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

// BLE-N1 (Advisor 2026-09-29): der BLE-Drain las bf_peek() und danach
// bf_tail_gen(). Verdraengt ein Schreiber (nRF52: OnRxDone im LORA-Task) den
// Frame A genau dazwischen, haelt der Leser A mit der Generation von B -- und
// pop()t B, der nie gesendet wurde. Hier die Verschraenkung von Hand.
static void push_a_b_then_evict_a(uint8_t *out, uint16_t outmax, bool atomic,
                                  uint16_t *gen)
{
    uint8_t a[20], b[20], c[30];
    fill(a, 20, 0x10);
    fill(b, 20, 0x40);
    fill(c, 30, 0x70);
    bf_push(&f, a, 20);   // 21 Byte
    bf_push(&f, b, 20);   // 42 Byte
    if (atomic)
        bf_peek_gen(&f, out, outmax, gen);
    else
        bf_peek(&f, out, outmax);
    // Schreiber: 31 Byte passen nur, wenn A (21) weicht -> Ende bei B.
    TEST_ASSERT_EQUAL_INT(1, bf_push(&f, c, 30));
    if (!atomic)
        *gen = bf_tail_gen(&f);   // alte Reihenfolge: Generation NACH dem Peek
}

static void test_old_peek_then_gen_pops_unsent_frame(void)
{
    uint8_t out[32];
    uint16_t gen = 0;
    push_a_b_then_evict_a(out, sizeof(out), false, &gen);
    expect_frame(out, 20, 0x10, "leser haelt A");
    // Die alte Pruefung "gleiche Generation?" sagt ja -- und nimmt B.
    TEST_ASSERT_EQUAL_UINT16(gen, bf_tail_gen(&f));
    bf_pop(&f);
    uint8_t next[32];
    TEST_ASSERT_EQUAL_UINT8(30, bf_peek(&f, next, sizeof(next)));   // B ist weg, ungesendet
}

static void test_peek_gen_and_pop_if_keep_unsent_frame(void)
{
    uint8_t out[32];
    uint16_t gen = 0;
    push_a_b_then_evict_a(out, sizeof(out), true, &gen);
    expect_frame(out, 20, 0x10, "leser haelt A");
    // A ist verdraengt: pop_if nimmt nichts, B bleibt als naechster Frame.
    TEST_ASSERT_FALSE(bf_pop_if(&f, gen));
    uint8_t next[32];
    TEST_ASSERT_EQUAL_UINT8(20, bf_peek(&f, next, sizeof(next)));
    expect_frame(next, 20, 0x40, "B bleibt");
    // Ohne Verdraengung nimmt pop_if genau den gelesenen Frame.
    uint16_t g2 = 0;
    bf_peek_gen(&f, next, sizeof(next), &g2);
    TEST_ASSERT_TRUE(bf_pop_if(&f, g2));
    TEST_ASSERT_EQUAL_UINT8(30, bf_peek(&f, next, sizeof(next)));
}

static void test_peek_gen_and_pop_if_take_the_lock(void)
{
    uint8_t a[5], out[8];
    uint16_t gen = 0;
    fill(a, 5, 0x20);
    bf_push(&f, a, 5);
    lock_calls = 0;
    bf_peek_gen(&f, out, sizeof(out), &gen);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lock_calls, "bf_peek_gen takes the ring lock once");
    bf_pop_if(&f, gen);
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, lock_calls, "bf_pop_if takes the ring lock once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lock_depth, "and releases it");
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

// --- Regressionen aus #1157 (f1c5b14f) --------------------------------------

// bf_iter_begin() las oldest/frames/evict_gen ungesperrt; eine Verdraengung
// dazwischen liess die Web-Nachrichtenseite Muell rendern.
static void test_iter_begin_reads_under_lock(void)
{
    uint8_t a[5];
    fill(a, 5, 0x20);
    bf_push(&f, a, 5);
    lock_calls = 0;

    bf_iter_t it;
    bf_iter_begin(&f, &it);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lock_calls, "bf_iter_begin takes the ring lock");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lock_depth, "and releases it");
}

// Der BLE-Config-Burst (bis 12 Frames zu 245 Byte) geht auf einmal in den
// Kommando-Ring. 3072 Byte halten ihn ohne Verlust; 2048 (vorher Klassik,
// S3, RAK) verlieren Frames -- die ersten, also das I-Register.
static int push_burst(byte_fifo_t *r)
{
    uint8_t b[245];
    int lost = 0;
    for (int i = 0; i < 12; i++)
    {
        fill(b, sizeof(b), (uint8_t)(i * 7));
        lost += bf_push(r, b, sizeof(b));
    }
    return lost;
}

static void test_config_burst_fits_3072_not_2048(void)
{
    static uint8_t big[3072];
    static uint8_t old[2048];
    byte_fifo_t rb = BYTE_FIFO_INIT(big);
    byte_fifo_t ro = BYTE_FIFO_INIT(old);

    TEST_ASSERT_EQUAL_INT(0, push_burst(&rb));
    TEST_ASSERT_EQUAL_UINT16(12, bf_unread(&rb));
    uint8_t out[256];
    TEST_ASSERT_EQUAL_UINT8(245, bf_peek(&rb, out, sizeof(out)));
    expect_frame(out, 245, 0, "first burst frame survives");

    TEST_ASSERT_TRUE_MESSAGE(push_burst(&ro) > 0, "2048 B cannot hold the worst-case burst");
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
    RUN_TEST(test_old_peek_then_gen_pops_unsent_frame);
    RUN_TEST(test_peek_gen_and_pop_if_keep_unsent_frame);
    RUN_TEST(test_peek_gen_and_pop_if_take_the_lock);
    RUN_TEST(test_history_iterates_oldest_to_newest_including_read);
    RUN_TEST(test_history_stops_after_eviction_mid_walk);
    RUN_TEST(test_unread_counts_frames_used_counts_bytes);
    RUN_TEST(test_reset_clears_everything);
    RUN_TEST(test_iter_begin_reads_under_lock);
    RUN_TEST(test_config_burst_fits_3072_not_2048);
    return UNITY_END();
}
