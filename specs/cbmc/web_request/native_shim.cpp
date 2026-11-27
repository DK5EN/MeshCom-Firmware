// Native stand-in for the nondet functions of harness.cpp (sanity run without ESBMC, not part of any job).
#include <stdlib.h>
extern "C" {
static const char *ints = "cookie:; \r\n\t mcsMCS=HTP/1.0-+09x";
static int patpos = 0;
unsigned char nondet_uchar()
{
    const char *pat = getenv("W5PAT");
    if (pat && pat[patpos])
    {
        const char c = pat[patpos++];
        return c == '~' ? (unsigned char)0 : (unsigned char)c; // '~' stands for a NUL byte (an environment string cannot hold one)
    }
    if (rand() % 10 < 6)
        return (unsigned char)ints[rand() % 32];
    return (unsigned char)(rand() & 0xff);
}
int nondet_int() { return rand(); }
int nd_range(int lo, int hi) { return lo + rand() % (hi - lo + 1); }
unsigned char nd_notcrlf(int allow_nul)
{
    for (;;)
    {
        unsigned char c = nondet_uchar();
        if (c != '\r' && c != '\n' && (allow_nul || c != 0))
            return c;
    }
}
}
// seed from W5SEED
__attribute__((constructor)) static void seed_it() { const char *s = getenv("W5SEED"); srand(s ? (unsigned)atoi(s) : 1u); }
