// FORMAL-01 harness-local model of hmac_sha256.cpp for remote_cmd proofs.
// sha256() / hmacSha256() are replaced by nondeterministic-output models that read EXACTLY their declared inputs
// (so out-of-bounds reads by the caller are still found) and record what they were called with. hexLower, ctEqual and
// hmac_sha256_detail::wipe are the REAL functions: the real hmac_sha256.cpp is included with the two heavy entry points renamed.
#include "hmac_sha256.h"
#include "hmac_model.h"

#define sha256 real_sha256
#define hmacSha256 real_hmacSha256
#include "hmac_sha256.cpp"
#undef sha256
#undef hmacSha256

extern "C" unsigned char nondet_uchar();

ModelState g_m;

void sha256(const uint8_t *data, size_t n, uint8_t out[32])
{
    volatile uint8_t sink = 0;
    for(size_t i = 0; i < n; i++) sink ^= data[i];
    (void)sink;
    g_m.sha_calls++;
    for(int i = 0; i < 32; i++) out[i] = nondet_uchar();
}

void hmacSha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32])
{
    volatile uint8_t sink = 0;
    for(size_t i = 0; i < klen; i++) sink ^= key[i];
    g_m.msg_len = mlen < sizeof(g_m.msg) ? mlen : sizeof(g_m.msg);
    for(size_t i = 0; i < mlen; i++) { sink ^= msg[i]; if(i < sizeof(g_m.msg)) g_m.msg[i] = msg[i]; }
    (void)sink;
    g_m.hmac_calls++;
    for(int i = 0; i < 32; i++) { out[i] = nondet_uchar(); g_m.mac[i] = out[i]; }
}

// strnlen model with the POSIX semantics: reads at most n bytes, stops at NUL
extern "C" size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while(i < n && s[i]) i++;
    return i;
}
