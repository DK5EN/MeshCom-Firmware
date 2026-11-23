// FORMAL-01 CBMC harness: parseConfFrame() (src/conf_frame.cpp) over every UDP CONF payload up to MAXLEN (255, UDP_TX_BUF_SIZE) bytes.
// Surface: UDP from the MeshCom server (src/udp_functions.cpp), untrusted bytes after the 4-byte "CONF" tag.
// Proves: no out-of-bounds read of payload, no write past ConfFrame, both strings NUL-terminated within their caps.
// Mutations (EXPECT=fail jobs):
//   -DMUT_SHORT_BUF  the buffer is one byte shorter than the length passed in -> a read past it must be found
//   -DWITNESS_ACCEPT assert that no frame is ever accepted -> must fail (the accept path is reachable)
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "conf_frame.h"

static bool nul_within(const char *s, unsigned cap)
{
    for(unsigned i = 0; i < cap; i++)
        if(s[i] == 0) return true;
    return false;
}

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
    uint8_t *buf = new uint8_t[len - 1];
    for(int i = 0; i < len - 1; i++) buf[i] = nondet_uchar();
#else
    uint8_t *buf = new uint8_t[len > 0 ? len : 1];
    for(int i = 0; i < len; i++) buf[i] = nondet_uchar();
#endif
    ConfFrame out;
    bool ok = parseConfFrame(buf, len, out);
    if(ok) {
        assert(nul_within(out.call, sizeof(out.call)));
        assert(nul_within(out.shortname, sizeof(out.shortname)));
    }
#ifdef WITNESS_ACCEPT
    assert(!ok);
#endif
    delete[] buf;
    return 0;
}
