// FORMAL-01 CBMC harness: FwJsonScan (src/fw_update_assets.h), the streaming scanner for the GitHub releases/latest JSON.
// Surface: HTTPS body in chunks (src/esp32/fw_update_net.cpp), length unbounded, content from a release JSON.
// Input = PRE (concrete prefix) + N free bytes (nondet) + POST (concrete suffix). Modes (macros from the .check files):
//   BYTEWISE (set by DOC/DEFNUL/PRIME/LEAK) feeds one byte per fwScanFeed call and checks the result after every byte: chunk splits are covered because
//                the scanner holds all state in FwJsonScan (fwScanFeed is a plain loop over the bytes); the default job covers a nondet 2-chunk split.
//   default      PRE = POST = empty, N free bytes, fed as two chunks split at a nondet point (chunk boundaries), result checked after each
//   DOC          PRE = a valid release document up to inside browser_download_url's value, POST = the rest: N free bytes corrupt/extend the middle
//   PRIME        PRE leaves the scanner inside a 250-char download url (cap 255): free bytes drive the cap edge with the real code
//   LEAK         two assets: PRE = first asset opened, N free bytes, POST = "},{" (second asset): byte by byte, at the second asset start
//                nothing of the first asset may survive (beginAsset reset)
//   DEFNUL       PRE+1 free byte+POST valid document: one free byte inside browser_download_url (with -DSTRICT_URL: url must be non-empty)
//   MUT_SHORT    the declared length is one more than the array holds (read past the array)
//   WITNESS      assert that no asset with a non-empty url is ever produced (must fail: the accept path is reachable)
// Result properties are O(1) asserts on purpose (a 256-iteration scan per call makes the solver time out); the unbounded step job
// (step.cpp) uses the full NUL scan.
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "fw_update_assets.h"

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

#ifndef NFREE
#define NFREE 6
#endif

#if defined(DOC) || defined(DEFNUL) || defined(PRIME) || defined(LEAK)
#define BYTEWISE
#endif

#if defined(DOC) || defined(DEFNUL)
#define PRE "{\"tag_name\":\"v\",\"assets\":[{\"name\":\"n\",\"size\":1,\"browser_download_url\":\""
#define POST "\"}]}"
#elif defined(PRIME)
#define PRE "{\"tag_name\":\"v\",\"assets\":[{\"name\":\"n\",\"size\":1,\"browser_download_url\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define POST "\"}]}"
#elif defined(LEAK)
#define PRE "{\"tag_name\":\"v\",\"assets\":[{\"name\":\"n\",\"size\":7,\"browser_download_url\":\"u\""
#define POST "},{"
#else
#define PRE ""
#define POST ""
#endif

#ifdef DEFNUL
#undef NFREE
#define NFREE 1
#endif

// Result properties. The scanner state must hold a NUL at url[urlLen] / tag[tagLen] inside the declared caps, so every consumer
// that reads the result as a C string is safe; the copied FwReleaseInfo is terminated at its last byte.
static void check_result(const FwJsonScan &s)
{
    FwReleaseInfo out;
    bool ok = fwScanResult(s, out);
    if(!ok) {
        assert(!out.found);
        return;
    }
    assert(s.tagLen <= sizeof(s.tag) - 1 && s.tag[s.tagLen] == 0);
    assert(out.tag[sizeof(out.tag) - 1] == 0);
    if(out.found) {
        assert(s.urlLen >= 1 && s.urlLen <= sizeof(s.url) - 1);
        assert(s.url[s.urlLen] == 0);
        assert(out.url[sizeof(out.url) - 1] == 0);
#ifdef STRICT_URL
        assert(out.url[0] != 0);   // an accepted asset has a usable (non-empty) url
#endif
    }
#ifdef WITNESS
    assert(!(out.found && out.url[0] != 0));
#endif
}

static FwJsonScan s;

#ifdef BYTEWISE
static void feed1(char c)
{
    bool was2 = (s.depth == 2) && s.inAssets && !s.found && !s.inStr && !s.err;
    fwScanFeed(s, &c, 1);
#ifdef LEAK
    if(was2 && c == '{' && !s.err) {
        // beginAsset() ran: nothing of the previous asset object may survive
        assert(s.depth == 3);
        assert(!s.candMatch && !s.candSize);
        assert(s.size == 0 && s.urlLen == 0 && !s.urlOvf && s.url[0] == 0 && s.dcnt == 0);
    }
#endif
    check_result(s);
}
#endif

int main()
{
    fwScanInit(s, "n");
    {
        const char *pre = PRE;
#ifdef BYTEWISE
        for(const char *p = pre; *p; p++) feed1(*p);
#else
        fwScanFeed(s, pre, strlen(pre));
#endif
        assert(!s.err);
    }

    static uint8_t fbuf[NFREE];
    for(int i = 0; i < NFREE; i++) fbuf[i] = nondet_uchar();
    int len = NFREE;
#ifdef MUT_SHORT
    len = NFREE + 1;   // declared length is one more than the array holds
#endif

#ifdef BYTEWISE
    for(int i = 0; i < len; i++) feed1((char)fbuf[i]);
    for(const char *p = POST; *p; p++) feed1(*p);
#else
    int k = nondet_int();
    __ESBMC_assume(k >= 0 && k <= len);
    fwScanFeed(s, (const char *)fbuf, (size_t)k);
    check_result(s);
    fwScanFeed(s, (const char *)fbuf + k, (size_t)(len - k));
    check_result(s);
    const char *post = POST;
    fwScanFeed(s, post, strlen(post));
#endif
    check_result(s);
    return 0;
}
