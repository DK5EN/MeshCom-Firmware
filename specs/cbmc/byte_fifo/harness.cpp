// FORMAL-01 CBMC harness: byte_fifo (src/byte_fifo.cpp), single-threaded, BF_LOCK is a no-op (no BF_TEST_LOCK_HOOK).
// Any sequence of K operations (push2 / peek / peek_gen / pop_if / pop / iterate begin+next) with nondet choice,
// nondet frame split (alen/blen) and nondet data (base byte per push, see below), capacity CAP (small, so wrap-around and eviction occur within K ops).
// A shadow model (absolute frame sequence numbers oldest <= tail <= head) is kept in the harness. Proves:
//   - no out-of-bounds access on the CAP-byte storage (esbmc bounds/pointer checks), no overflow
//   - head/tail/oldest < cap, used <= cap, head == pos(oldest)+used, tail == position of the first unread frame
//   - push returns exactly the number of UNREAD frames evicted; frames/unread/used equal the shadow after every op
//   - every peeked / peek_gen / iterated frame equals the pushed bytes (also across wrap-around), full length returned
//   - pop_if succeeds iff the generation taken by peek_gen is still current (tail did not move since)
//   - the iterator stops (0) once an eviction happened since bf_iter_begin
// Mutations (EXPECT=fail):
//   -DMUT_CAP_PLUS1     the ring claims cap = CAP+1 over a CAP-byte storage -> out of bounds write must be found
//   -DWITNESS_EVICT     assert no unread frame is ever evicted -> must fail (eviction path reachable)
//   -DWITNESS_WRAP      assert a frame never wraps the end of storage -> must fail (wrap path reachable)
//   -DWITNESS_POPIF     assert pop_if never succeeds -> must fail
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "byte_fifo.h"

#ifndef CAP
#define CAP 8
#endif
#ifndef K
#define K 4
#endif
#ifndef MAXPART
#define MAXPART 3
#endif

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

static int pick(int lo, int hi)
{
    int v = nondet_int();
    __ESBMC_assume(v >= lo && v <= hi);
    return v;
}

struct Sh { uint8_t d[2 * MAXPART]; uint8_t len; uint16_t pos; };
static Sh sh[K + 1];
static int s_old, s_tail, s_head; // absolute sequence numbers
static unsigned s_used, s_evicts;
static uint16_t s_hpos; // ring position of the next free byte

static void check(const byte_fifo_t &f)
{
    assert(f.cap <= CAP); // the ring must not claim more storage than it was given
    assert(f.head < f.cap && f.tail < f.cap && f.oldest < f.cap);
    assert(f.used <= f.cap);
    assert(f.frames == s_head - s_old);
    assert(f.unread == s_head - s_tail);
    assert(f.used == s_used);
    if(s_head > s_old) assert(f.oldest == sh[s_old].pos);
    if(s_head > s_tail) assert(f.tail == sh[s_tail].pos);
    assert((f.oldest + f.used) % f.cap == f.head);
    assert(f.head == s_hpos);
    if(s_head == s_old) assert(f.oldest == s_hpos);
    assert(bf_empty(&f) == (s_head == s_tail));
}

static void same(const uint8_t *out, int seq, unsigned n)
{
    for(unsigned i = 0; i < n; i++) assert(out[i] == sh[seq].d[i]);
}

int main()
{
    uint8_t storage[CAP];
#ifdef MUT_CAP_PLUS1
    byte_fifo_t f = { storage, (uint16_t)(CAP + 1), 0, 0, 0, 0, 0, 0, 0, 0 };
#else
    byte_fifo_t f = { storage, (uint16_t)CAP, 0, 0, 0, 0, 0, 0, 0, 0 };
#endif
    s_old = s_tail = s_head = 0; s_used = 0; s_evicts = 0; s_hpos = 0;
    bool have_gen = false; uint16_t gen = 0; int gen_tail = 0;
    bf_iter_t it; bool it_on = false; int it_seq = 0, it_left = 0; unsigned it_ev = 0;

    for(int step = 0; step < K; step++) {
        int op = pick(0, 6);
        if(op == 0 && s_head < K) { // push2
            uint8_t a[MAXPART], b[MAXPART];
            // data: one nondet base byte per push, bytes base+i: distinct enough to expose reordering / wrap / overwrite errors
            // without one free symbolic byte per cell (state explosion); order and wrap mistakes still change the values
            uint8_t base = nondet_uchar();
            for(int i = 0; i < MAXPART; i++) { a[i] = (uint8_t)(base + i); b[i] = (uint8_t)(base + 16 + i); }
            int alen = pick(0, MAXPART), blen = pick(0, MAXPART);
            int len = alen + blen;
            bool nullb = (blen == 0) && nondet_int();
            int r = bf_push2(&f, a, (uint8_t)alen, nullb ? NULL : b, (uint8_t)blen);
            if(len == 0 || len + 1 > CAP) {
                assert(r == -1);
            } else {
                unsigned need = len + 1;
                int lost = 0;
                while(CAP - s_used < need) {
                    if(s_old == s_tail) { s_tail++; lost++; }
                    s_used -= sh[s_old].len + 1; s_old++; s_evicts++;
                }
                assert(r == lost);
#ifdef WITNESS_EVICT
                assert(lost == 0);
#endif
                uint16_t pos = s_hpos;
                sh[s_head].len = len; sh[s_head].pos = pos;
                for(int i = 0; i < alen; i++) sh[s_head].d[i] = a[i];
                for(int i = 0; i < blen; i++) sh[s_head].d[alen + i] = b[i];
#ifdef WITNESS_WRAP
                assert(pos + len + 1 <= CAP);
#endif
                s_head++; s_used += need; s_hpos = (uint16_t)((pos + need) % CAP);
            }
        } else if(op == 1 || op == 2) { // peek / peek_gen
            uint8_t out[2 * MAXPART]; for(int i = 0; i < 2 * MAXPART; i++) out[i] = 0;
            int outmax = pick(0, 2 * MAXPART);
            uint8_t l;
            uint16_t g = 0;
            if(op == 1) l = bf_peek(&f, out, (uint16_t)outmax);
            else { l = bf_peek_gen(&f, out, (uint16_t)outmax, &g); have_gen = true; gen = g; gen_tail = s_tail; }
            if(s_head == s_tail) assert(l == 0);
            else { assert(l == sh[s_tail].len); same(out, s_tail, outmax < l ? outmax : l); }
        } else if(op == 3) { // pop_if with a nondet-aged generation
            if(have_gen) {
                bool ok = bf_pop_if(&f, gen);
                bool exp = (s_head > s_tail) && gen_tail == s_tail;
                assert(ok == exp);
#ifdef WITNESS_POPIF
                assert(!ok);
#endif
                if(ok) s_tail++;
            }
        } else if(op == 4) { // pop
            bf_pop(&f);
            if(s_head > s_tail) s_tail++;
        } else if(op == 5) { // iterator begin
            bf_iter_begin(&f, &it); it_on = true; it_seq = s_old; it_left = s_head - s_old; it_ev = s_evicts;
        } else if(op == 6 && it_on) { // iterator next
            uint8_t out[2 * MAXPART]; for(int i = 0; i < 2 * MAXPART; i++) out[i] = 0;
            int outmax = pick(0, 2 * MAXPART);
            uint8_t l = bf_iter_next(&f, &it, out, (uint16_t)outmax);
            if(it_left > 0 && it_ev == s_evicts) {
                assert(l == sh[it_seq].len); same(out, it_seq, outmax < l ? outmax : l);
                it_seq++; it_left--;
            } else {
                assert(l == 0); it_left = 0;
            }
        }
        check(f);
    }
    return 0;
}
