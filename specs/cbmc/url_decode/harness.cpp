// FORMAL-01 harness: url_percent_decode() (src/url_decode.cpp) for every input of 0..MAXLEN bytes.
// Surface: WebUI query parameters via decodeURLPercentCoding (web_functions.cpp), untrusted bytes.
// Contract: buf has len+1 bytes (decode in place, NUL slot included).
// Proves: no write/read outside buf[0..len], ret <= len, buf[ret] == 0. A raw NUL byte in the input is copied through (only %00 is dropped); not a defect, callers pass C strings.
// Modes:
//   (default)       the memory-safety + termination property over fully nondet content (malformed %, %4, %zz, %00 included)
//   -DFUNC          input is exactly "%XX" with XX a nondet valid hex pair (either case), printable result: ret == 1, buf[0] == value
//   -DMUT_CAP       buffer holds len bytes only (capacity one smaller than the contract) -> write of the NUL must be caught
//   -DWITNESS_DECODE  as FUNC, but asserts the escape is NOT decoded -> must fail
#include <stddef.h>
#include <assert.h>
#include "url_decode.h"

#ifndef MAXLEN
#define MAXLEN 24
#endif

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

static bool is_hex(unsigned char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static int hv(unsigned char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

int main()
{
#if defined(FUNC) || defined(WITNESS_DECODE)
    size_t len = 3;
    char *buf = new char[len + 1];
    unsigned char h = nondet_uchar(), l = nondet_uchar();
    __ESBMC_assume(is_hex(h) && is_hex(l));
    int v = (hv(h) << 4) | hv(l);
    __ESBMC_assume(v >= 0x20 && v != 0x7F && v != '"');
    buf[0] = '%'; buf[1] = (char)h; buf[2] = (char)l; buf[3] = 0;
    size_t r = url_percent_decode(buf, len);
    assert(r == 1);
    assert((unsigned char)buf[0] == v);
    assert(buf[1] == 0);
#ifdef WITNESS_DECODE
    assert(r != 1);
#endif
#else
    int n = nondet_int();
    __ESBMC_assume(n >= 0 && n <= MAXLEN);
    size_t len = (size_t)n;
#ifdef MUT_CAP
    char *buf = new char[len > 0 ? len : 1];   // one byte short for the terminator when len bytes survive
    __ESBMC_assume(len >= 1);
#else
    char *buf = new char[len + 1];
#endif
    for (size_t i = 0; i < len; i++) buf[i] = (char)nondet_uchar();
    size_t r = url_percent_decode(buf, len);
    assert(r <= len);
    assert(buf[r] == 0);
#endif
    delete[] buf;
    return 0;
}
