// FORMAL-01 ESBMC harness: the contract decodeAPRS() relies on from charset_filter_apply() (src/charset_filter.cpp), PLAIN mode:
// for any len bytes (exact-size allocation) the call stays inside the buffer and returns at most len (in-place filter never grows).
// That is what makes `cConcat1[iConcat1] = 0` in decodeAPRS() safe; pass.check models the filter with exactly this contract.
#include <assert.h>
#include <stddef.h>
#include "charset_filter.h"

#ifndef MAXLEN
#define MAXLEN 255
#endif

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

int main()
{
    int len = nondet_int();
    __ESBMC_assume(len >= 0 && len <= MAXLEN);
#ifdef MUT_SHORT_BUF
    __ESBMC_assume(len >= 1);
    char *buf = new char[len - 1];
    for(int i = 0; i < len - 1; i++) buf[i] = (char)nondet_uchar();
#else
    char *buf = new char[len > 0 ? len : 1];
    for(int i = 0; i < len; i++) buf[i] = (char)nondet_uchar();
#endif
    size_t r = charset_filter_apply(buf, (size_t)len, CHARSET_FILTER_PLAIN);
    assert(r <= (size_t)len);
    delete[] buf;
    return 0;
}
