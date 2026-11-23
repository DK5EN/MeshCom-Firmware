// FORMAL-01 CBMC harness: RM1 (remote management over RF) receive path = rm_runtime.cpp rmHandle sequence
//   rmParse(text, cmd)  ->  rmCheck(state, cmd, dst, src, passwd, minTx, maxTxx, now)  ->  switch(verdict) { dispatch }
// (src/remote_cmd.cpp). Surface: the DM text of a received frame (printable-ASCII bytes from RF, untrusted), sender call, passwd of the node.
// hmacSha256()/sha256() are nondet-output models that read exactly their declared input (hmac_model.cpp); hexLower/ctEqual are real.
// Two text generators: free (TEXTMAX bytes, NUL anywhere) and -DSTRUCT (frame skeleton, nondet field lengths/bytes, up to 90 bytes).
// "Dispatch" = the verdicts the runtime acts on (execute / refuse-and-answer / sync reply / cached re-send): OK, REFUSED, SYNC, CACHED.
// Proves for every text up to TEXTMAX bytes (NUL at any position), ROUNDS consecutive rmCheck calls on the same frame:
//   - memory safety / no UB in rmParse, rmCheck, rmCanonical, rmAccept, rmCommandAllowed (esbmc bounds, pointer, overflow checks)
//   - rmParse output: cmd/args/tag NUL-terminated within their arrays, tag exactly 16 lower-case hex, ctr==0 only for "sync"
//   - AUTH: dispatch  =>  hmacSha256 was called in this check, over exactly rmCanonical(cmd,dst,src), and its first 8 output bytes in
//     lower-case hex equal cmd.tag (i.e. the verify returned true); RM_OK additionally => ctr > hwm
//   - EMPTY SECRET (passwd NULL / "" / spaces only): verdict RM_REJ_DISABLED, no hmac/sha call, never dispatched (audit M1)
// Modes (-D): default AUTH; EMPTY_SECRET; mutations/witnesses (EXPECT=fail):
//   -DDIRECT            skip text+rmParse, hand rmCheck a fully nondet RmCmd (superset of any parser output; cheap)
//   -DPARSE_ONLY        stop after the rmParse contract checks (memory safety + output shape of the parser only)
//   -DROUNDS=n          number of rmCheck rounds on the same state (default 1; 2 makes the reply cache / replay paths reachable)
//   -DSTRUCT            (any mode) frame-shaped text with nondet fields instead of free text (see below)
//   -DWITNESS_PARSED    assert rmParse never accepts -> must fail (accept path reachable)
//   -DWITNESS_DISPATCH  assert no frame is ever dispatched -> must fail (RM_OK/REFUSED/SYNC/CACHED reachable)
//   -DWITNESS_OK        assert verdict != RM_OK -> must fail
//   -DMUT_NOAUTH        assert dispatch happens only when the hmac model was NOT called (opposite of the auth property) -> must fail
//   -DMUT_TAGFLIP       assert at dispatch that the tag does NOT equal hexLower(mac) (opposite) -> must fail
//   -DMUT_SHORT_TEXT    text buffer one byte shorter than it is read (no terminator) -> read past it must be found
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "remote_cmd.h"
#include "hmac_sha256.h"
#include "hmac_model.h"

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();
extern "C" unsigned nondet_uint();

#ifndef ROUNDS
#define ROUNDS 1
#endif
#ifndef CTRD
#define CTRD 11
#endif
#ifndef TEXTMAX
#define TEXTMAX 32
#endif

static bool is_dispatch(RmVerdict v) { return v == RM_OK || v == RM_REFUSED || v == RM_SYNC || v == RM_CACHED; }

static void nondet_str(char *p, int cap) // cap bytes, NUL-terminated somewhere inside, printable-ish nondet content
{
    for(int i = 0; i < cap; i++) p[i] = (char)nondet_uchar();
    p[cap - 1] = 0;
}

int main()
{
#ifdef STRUCT
    // frame-shaped text: "RM1 " ctr(1..CTRD digits, nondet) ' ' cmd(1..16 bytes) [' ' args(1..41 bytes)] ' ' tag(16 bytes), every field byte nondet.
    // Covers the field caps (cmd 15, args 39, 32-bit ctr overflow) that TEXTMAX-bounded free text cannot reach. Too long/short fields included.
    char text[4 + CTRD + 1 + 16 + 1 + 41 + 1 + 16 + 1];
    {
        int cd = nondet_int(), cl = nondet_int(), al = nondet_int();
        __ESBMC_assume(cd >= 1 && cd <= CTRD && cl >= 1 && cl <= 16 && al >= 0 && al <= 41);
        int p = 0;
        text[p++] = 'R'; text[p++] = 'M'; text[p++] = '1'; text[p++] = ' ';
        for(int i = 0; i < cd; i++) text[p++] = (char)nondet_uchar();
        text[p++] = ' ';
        for(int i = 0; i < cl; i++) text[p++] = (char)nondet_uchar();
        if(al > 0) { text[p++] = ' '; for(int i = 0; i < al; i++) text[p++] = (char)nondet_uchar(); }
        text[p++] = ' ';
        for(int i = 0; i < 16; i++) text[p++] = (char)nondet_uchar();
        text[p] = 0;
    }
#else
    char text[TEXTMAX + 1];
    nondet_str(text, TEXTMAX + 1);
#endif
    char dst[11], src[11];
    nondet_str(dst, 11); nondet_str(src, 11);
    char pw[7];
#ifdef EMPTY_SECRET
    // NULL, "", or spaces only
    int k = nondet_int();
    __ESBMC_assume(k >= -1 && k <= 5);
    for(int i = 0; i < 6; i++) pw[i] = ' ';
    pw[6] = 0; pw[k < 0 ? 0 : k] = 0;
    const char *passwd = (k < 0) ? NULL : pw;
#else
    nondet_str(pw, 7);
    const char *passwd = pw;
#endif
    int minTx = nondet_int(), maxTx = nondet_int();
    __ESBMC_assume(minTx >= -5 && minTx <= 30 && maxTx >= -5 && maxTx <= 30);

#ifdef MUT_SHORT_TEXT
    char *t = new char[TEXTMAX];
    for(int i = 0; i < TEXTMAX; i++) t[i] = text[i]; // unterminated: strlen-style scan in rmParse runs off the end
    const char *tp = t;
#else
    const char *tp = text;
#endif

    RmCmd cmd;
#ifdef DIRECT
    // rmCheck on an arbitrary RmCmd (every byte nondet, no NUL / hex / ctr contract assumed): a superset of everything rmParse can emit
    cmd.ctr = nondet_uint();
    for(unsigned i = 0; i < sizeof(cmd.cmd); i++) cmd.cmd[i] = (char)nondet_uchar();
    for(unsigned i = 0; i < sizeof(cmd.args); i++) cmd.args[i] = (char)nondet_uchar();
    for(unsigned i = 0; i < sizeof(cmd.tag); i++) cmd.tag[i] = (char)nondet_uchar();
#else
    bool parsed = rmParse(tp, cmd);
#ifdef WITNESS_PARSED
    assert(!parsed);
#endif
    if(!parsed) return 0;

    // rmParse output contract
    assert(cmd.tag[16] == 0);
    for(int i = 0; i < 16; i++) assert((cmd.tag[i] >= '0' && cmd.tag[i] <= '9') || (cmd.tag[i] >= 'a' && cmd.tag[i] <= 'f'));
    bool cmd_nul = false, args_nul = false;
    for(unsigned i = 0; i < sizeof(cmd.cmd); i++) if(cmd.cmd[i] == 0) cmd_nul = true;
    for(unsigned i = 0; i < sizeof(cmd.args); i++) if(cmd.args[i] == 0) args_nul = true;
    assert(cmd_nul && args_nul);
    assert(cmd.ctr != 0 || strcmp(cmd.cmd, "sync") == 0);

#endif
#ifdef PARSE_ONLY
    return 0;
#endif
    RmState s;
    uint32_t hwm = nondet_uint();
    rmStateInit(s, hwm);
    s.strict = nondet_int() != 0;

    for(int round = 0; round < ROUNDS; round++) {
        uint32_t now = nondet_uint();
        g_m.sha_calls = g_m.hmac_calls = 0; g_m.msg_len = 0;
        uint32_t hwm_before = s.hwm;
        RmVerdict v = rmCheck(s, cmd, dst, src, passwd, minTx, maxTx, now);
#ifdef EMPTY_SECRET
        assert(v == RM_REJ_DISABLED);
        assert(g_m.hmac_calls == 0 && g_m.sha_calls == 0);
        assert(!is_dispatch(v));
#else
        if(is_dispatch(v)) {
#ifdef WITNESS_DISPATCH
            assert(0);
#endif
#ifdef MUT_NOAUTH
            assert(g_m.hmac_calls == 0);
#endif
            assert(g_m.hmac_calls >= 1);
            char canon[160];
            size_t cl = rmCanonical(cmd, dst, src, canon, sizeof(canon));
            assert(cl > 0 && cl == g_m.msg_len && memcmp(canon, g_m.msg, cl) == 0);
            char hex[17];
            hexLower(g_m.mac, 8, hex);
#ifdef MUT_TAGFLIP
            assert(memcmp(hex, cmd.tag, 16) != 0);
#endif
            assert(memcmp(hex, cmd.tag, 16) == 0);
        }
        if(v == RM_OK) {
#ifdef WITNESS_OK
            assert(0);
#endif
            assert(cmd.ctr > hwm_before);
            // runtime contract: rmAccept for every RM_OK; second round can then hit the cache
            char res[RM_MAX_RESULT + 1]; strcpy(res, "ok");
            rmAccept(s, cmd, res, now);
        }
        if(v == RM_REFUSED) { char res[RM_MAX_RESULT + 1]; strcpy(res, "err"); rmAccept(s, cmd, res, now); }
        if(v == RM_REJ_DISABLED) assert(g_m.hmac_calls == 0); // only empty secrets
#endif
    }
    return 0;
}
