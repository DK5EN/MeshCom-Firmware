// FORMAL-01 CBMC harness: one input byte of FwJsonScan for ANY scanner state satisfying an inductive invariant.
// Unbounded in input length: if INV holds before a byte it holds after, and the byte step is memory safe and UB free,
// then by induction (base case: fwScanInit state, asserted below) every input of any length is safe.
// Covers fwScanFeed (one loop iteration, nondet byte), stringChar, primitiveChar, emit, startString, endString,
// beginAsset, endAsset, and fwScanResult on every invariant state.
// Mutation (EXPECT=fail job): -DMUT_CAP lets the invariant admit urlLen == 256 (cap + 1): emit/endString must go out of bounds.
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "fw_update_assets.h"

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

static bool nul_within(const char *s, unsigned cap)
{
    for(unsigned i = 0; i < cap; i++)
        if(s[i] == 0) return true;
    return false;
}

// Invariant of FwJsonScan (what the code maintains between bytes).
static bool inv(const FwJsonScan &s)
{
    using namespace fwscan_detail;
#ifdef MUT_CAP
    const unsigned urlCap = sizeof(s.url);        // 256: one too many
#else
    const unsigned urlCap = sizeof(s.url) - 1;    // 255
#endif
    bool urlOpen = s.inStr && s.target == T_URL;
    if(s.depth > 32) return false;
    if(s.keyLen > sizeof(s.key) - 1) return false;
    if(s.tagLen > sizeof(s.tag) - 1) return false;
    if(s.urlLen > urlCap) return false;
    if(s.escState > 2 || s.hexCnt > 4 || (s.escState == 2 && s.hexCnt > 3)) return false;
    if(s.target > T_URL || s.pendKey > K_URL) return false;
    if(!(s.dcnt <= 71 || s.dcnt == 255)) return false;
    if(s.want[sizeof(s.want) - 1] != 0) return false;
    if(s.nmPos > sizeof(s.want) - 1) return false;
    for(unsigned i = 0; i < sizeof(s.want); i++)
        if(i < s.nmPos && s.want[i] == 0) return false;
    // a closed url string leaves a NUL at url[urlLen] (found result relies on it)
    if(!urlOpen && s.urlLen <= sizeof(s.url) - 1 && s.url[s.urlLen] != 0) return false;
    if(s.found && (urlOpen || s.pendKey == K_URL || s.urlLen == 0 || s.urlOvf)) return false;
    if(s.isKey && !(s.inStr && s.target == T_KEY)) return false;   // a key string is open only as T_KEY
    // the captured url is only open while the key/url state machine says so
    if(s.target == T_URL && !s.inStr) return false;
    return true;
}

int main()
{
    // base case
    {
        FwJsonScan z;
        fwScanInit(z, "n");
        assert(inv(z));
    }
    FwJsonScan s;
    // arbitrary state: every byte nondet, then constrained by inv
    unsigned char *raw = (unsigned char *)&s;
    for(unsigned i = 0; i < sizeof(s); i++) raw[i] = nondet_uchar();
    // bool members must hold 0/1 (a C++ bool with another value is UB)
    bool *bp[] = { &s.err, &s.started, &s.inStr, &s.isKey, &s.expectKey, &s.inAssets, &s.primStarted, &s.wantOk, &s.nmOk,
                   &s.tagOk, &s.tagOvf, &s.keyOvf, &s.urlOvf, &s.candMatch, &s.candSize, &s.found, &s.draft, &s.prerelease };
    for(unsigned i = 0; i < sizeof(bp) / sizeof(bp[0]); i++) {
        unsigned char *b = (unsigned char *)bp[i];
        __ESBMC_assume(*b <= 1);
    }
    __ESBMC_assume(inv(s));
    // key[] is NUL-terminated by endString before strcmp; model that: strcmp only sees key after key[keyLen]=0, so no assumption needed.
    char c = (char)nondet_uchar();
    fwScanFeed(s, &c, 1);
    assert(inv(s));
    // result fields on every reachable-by-invariant state
    FwReleaseInfo out;
    bool ok = fwScanResult(s, out);
    if(ok) {
        assert(nul_within(out.tag, sizeof(out.tag)));
        if(out.found) {
            assert(nul_within(out.url, sizeof(out.url)));
#ifdef STRICT_URL
            assert(out.url[0] != 0);
#endif
        }
    }
    return 0;
}
