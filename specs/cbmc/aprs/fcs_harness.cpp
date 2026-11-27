// B-APRS-PATH-01 ESBMC harness: aprsFcsSum() (src/aprs_path_scan.h), the frame check sequence of decodeAPRS(), on its
// own. Proves for every buffer of 0..255 bytes and every n <= its length: memory safety (reads only buf[0..n)), no
// overflow of the unsigned sum (255 * 255 fits). The full-frame decodeAPRS() proof (pass.check) uses this contract.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include "aprs_path_scan.h"

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

int main()
{
    int len = nondet_int();
    __ESBMC_assume(len >= 0 && len <= 255);
    uint8_t *buf = new uint8_t[len > 0 ? len : 1];
    for(int i = 0; i < len; i++) buf[i] = nondet_uchar();
    int n = nondet_int();
    __ESBMC_assume(n >= 0 && n <= len);
    const unsigned int s = aprsFcsSum(buf, (uint16_t)n);
    assert(s <= 255u * (unsigned int)n);
    delete[] buf;
    return 0;
}
