// FORMAL-01 / B-RM-RANGE-01 (2026-11-27): rmCanonical() on its own, the contract the rmCheck() proofs use under
// -DCANON_CONTRACT (harness.cpp). Inputs as in the DIRECT jobs: every byte of the RmCmd nondet (no NUL, ctr or hex
// contract), dst and src nondet strings of at most 10 characters, any output size n of 0..160 (rmCheck uses 160).
// Proves: memory safety of the builder (reads of cmd/args/dst/src, writes of out[0..n)), and
//   - failure: returns 0 and, for n > 0, out[0] == 0
//   - success: 1 <= len < n, out[len] == 0, len <= 94 (the CANON_MAX the contract assumes for these inputs: cmd and
//     args without a terminator are copied up to their array size, 16 + 40; rmCheck rejects those before it builds)
// -DMUT_CANON_MAX93 asserts len <= 93 instead -> must fail (the bound 94 is reached, so it is tight).
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "remote_cmd.h"

extern "C" unsigned char nondet_uchar();
extern "C" unsigned nondet_uint();

static void nondet_str(char *p, int cap)
{
    for(int i = 0; i < cap; i++) p[i] = (char)nondet_uchar();
    p[cap - 1] = 0;
}

int main()
{
    RmCmd cmd;
    cmd.ctr = nondet_uint();
    for(unsigned i = 0; i < sizeof(cmd.cmd); i++) cmd.cmd[i] = (char)nondet_uchar();
    for(unsigned i = 0; i < sizeof(cmd.args); i++) cmd.args[i] = (char)nondet_uchar();
    for(unsigned i = 0; i < sizeof(cmd.tag); i++) cmd.tag[i] = (char)nondet_uchar();
    char dst[11], src[11];
    nondet_str(dst, 11);
    nondet_str(src, 11);
    char out[160];
    size_t n = nondet_uint();
    __ESBMC_assume(n <= sizeof(out));
    const size_t len = rmCanonical(cmd, dst, src, out, n);
    if(len == 0)
    {
        if(n > 0)
            assert(out[0] == 0);
    }
    else
    {
        assert(len < n);
        assert(out[len] == 0);
#ifdef MUT_CANON_MAX93
        assert(len <= 93);
#else
        assert(len <= 94);
#endif
    }
    return 0;
}
