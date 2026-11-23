// FORMAL-01 CBMC harness: AppLink websocket client-frame decoder alWsFeed() (src/applink_ws.cpp, header/length validation,
// PH_EXT/PH_MASK/PH_PAYLOAD, headerDone, frameDone). Surface: bytes from the LAN app (untrusted TCP stream).
// Two modes:
//   default (MODE_ANY)   arbitrary byte stream of STREAM bytes (covers all of: 7-bit, 16-bit and 64-bit length forms, masks, opcodes,
//                        fragmentation state machine, control frames, close codes), fed in arbitrary chunks, re-fed with the unconsumed rest
//                        after every event like the real caller does.
//   -DMODE_BIG           well-formed masked frames around APPLINK_MAX_IN (300), see the generator below; -DBIG_SINGLE = one frame.
//   -DBIG_COARSE         (MODE_BIG fragmented, BIG_FIXED) one onData call per frame instead of header|payload
// Proves: no out-of-bounds read of the input or write of msg[]/ctl[]/mask[], no signed/shift overflow, msg_len <= APPLINK_MAX_IN,
// ctl_len <= 125, remaining never exceeds the declared length, a decoder that returns no event and was given bytes consumed >= 1 (progress),
// consumed <= len, events other than ERROR keep the buffers within bounds, error is sticky.
// Mutations (EXPECT=fail):
//   -DMUT_SHORT_IN   the input buffer is one byte shorter than the length handed to alWsFeed -> read past it must be found
//   -DWITNESS_MSG    assert no complete message is ever reported -> must fail (BINARY/TEXT is reachable)
//   -DWITNESS_BIG    (MODE_BIG) assert the 300 byte message never completes -> must fail (full buffer reachable)
//   -DWITNESS_ERR    assert no 1009 error -> must fail (too-big path reachable)
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "applink_ws.h"

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

#ifndef BIGCALLS
#define BIGCALLS 3
#endif
#ifndef STREAM
#define STREAM 10
#endif

static AlWsDecoder g_d;

static void inv(const AlWsDecoder &d)
{
    assert(d.msg_len <= APPLINK_MAX_IN);
    assert(d.ctl_len <= 125);
    assert(d.phase <= 5);
    assert(d.mask_got <= 4);
    assert(d.ext_need <= 8);
}

// feed the stream in arbitrary chunks, the caller loop handles events and re-feeds the unconsumed rest
static void drive(const uint8_t *in, int total, int maxcalls, const int *plan = 0)
{
    alWsDecoderInit(&g_d);
    int off = 0;
    bool err = false;
    for(int call = 0; call < maxcalls && off < total; call++) {
        int chunk;
        if(plan) chunk = (plan[call] > 0 && plan[call] <= total - off) ? plan[call] : total - off; // fixed chunk plan, last one takes the rest
        else { chunk = nondet_int(); __ESBMC_assume(chunk >= 1 && chunk <= total - off); }
        AlWsEvent ev;
#ifdef MUT_SHORT_IN
        size_t used = alWsFeed(&g_d, in + off, (size_t)chunk, &ev);
#else
        size_t used = alWsFeed(&g_d, in + off, (size_t)chunk, &ev);
#endif
        assert((int)used <= chunk);
        inv(g_d);
        if(err) { assert(used == 0 && ev == AL_WS_ERROR); }
        if(ev == AL_WS_ERROR) {
            assert(g_d.error_code == 1002 || g_d.error_code == 1009);
            err = true;
#ifdef WITNESS_ERR
            assert(g_d.error_code != 1009);
#endif
        } else {
            assert(used >= 1); // progress: otherwise the caller loop would spin
        }
        if(ev == AL_WS_TEXT || ev == AL_WS_BINARY) {
#ifdef WITNESS_MSG
            assert(0);
#endif
#ifdef WITNESS_BIG
            assert(g_d.msg_len != APPLINK_MAX_IN);
#endif
            assert(g_d.msg_len <= APPLINK_MAX_IN);
        }
        if(ev == AL_WS_PING || ev == AL_WS_PONG || ev == AL_WS_CLOSE) assert(g_d.ctl_len <= 125);
        off += (int)used;
        if(used == 0) break;
    }
}

int main()
{
#ifdef MODE_BIG
    // masked frames, 16-bit length form, mask 00 (output == input), payload bytes 0 (content is irrelevant for memory safety).
    // BIG_SINGLE: one BIN frame FIN=1, length L1 in 297..302 around APPLINK_MAX_IN (300): msg[300] filled to the last byte, 1009 beyond.
    // otherwise : BIN FIN=0 with L1 = 150, then CONT FIN=1 with L2 in 148..152: the reassembly limit (ext_len + msg_len > 300) is
    //             hit exactly at the boundary.
#ifdef BIG_SINGLE
    int L1 = nondet_int(), L2 = 0;
    __ESBMC_assume(L1 >= 297 && L1 <= 302);
    int total = 8 + L1;
#else
    int L1 = 150, L2 = nondet_int();
#ifndef L2MIN
#define L2MIN 148
#define L2MAX 152
#endif
    __ESBMC_assume(L2 >= L2MIN && L2 <= L2MAX);
    int total = 8 + L1 + 8 + L2;
#endif
    uint8_t *in = new uint8_t[total];
    int p = 0;
#ifdef BIG_SINGLE
    in[p++] = 0x82;
#else
    in[p++] = 0x02;
#endif
    in[p++] = 0xFE; in[p++] = (uint8_t)(L1 >> 8); in[p++] = (uint8_t)L1; in[p++] = 0; in[p++] = 0; in[p++] = 0; in[p++] = 0;
    for(int i = 0; i < L1; i++) in[p++] = 0;
#ifndef BIG_SINGLE
    in[p++] = 0x80; in[p++] = 0xFE; in[p++] = (uint8_t)(L2 >> 8); in[p++] = (uint8_t)L2; in[p++] = 0; in[p++] = 0; in[p++] = 0; in[p++] = 0;
    for(int i = 0; i < L2; i++) in[p++] = 0;
#endif
#ifdef BIG_FIXED
    // fixed chunking (header | payload | ...): one nondet length is the only free input, keeps the 300-byte payload loop concrete
#ifdef BIG_SINGLE
    const int plan[2] = { 8, 0 };
#elif defined(BIG_COARSE)
    // one call per frame (2 calls): the 4-call header|payload plan needed > 25 GB in ESBMC (2026-11-23)
    const int plan[2] = { 8 + L1, 0 };
#else
    const int plan[4] = { 8, L1, 8, 0 };
#endif
    drive(in, total, BIGCALLS, plan);
#else
    drive(in, total, BIGCALLS);
#endif
#else
    uint8_t in[STREAM];
    for(int i = 0; i < STREAM; i++) in[i] = nondet_uchar();
    int total = nondet_int();
    __ESBMC_assume(total >= 1 && total <= STREAM);
#ifdef MUT_SHORT_IN
    uint8_t *sh = new uint8_t[total - 1 > 0 ? total - 1 : 1];
    for(int i = 0; i < total - 1; i++) sh[i] = in[i];
    drive(sh, total, 3);
#else
    drive(in, total, 3);
#endif
#endif
    return 0;
}
