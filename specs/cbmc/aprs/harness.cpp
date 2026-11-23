// FORMAL-01 ESBMC harness: decodeAPRS() (src/aprs_functions.cpp) over every RF/UDP frame of rsize 0..255 and any byte content.
// Surface: LoRa RX, UDP RX (server and ext-UDP). Untrusted bytes; callers cap rsize at UDP_TX_BUF_SIZE (255).
// The firmware TU is included whole (single TU, so the harness-local strnlen in stubs/prelude.h is visible);
// String is the bounded stub in stubs/Arduino.h. Real code linked: charset_filter.cpp only with -DREAL_CHARSET. Modelled: checkRegexCall/isSpecialTargetCall (nondet bool).
// Mutations (EXPECT=fail jobs):
//   -DMUT_SHORT_BUF  buffer one byte shorter than rsize -> an out-of-bounds read must be found
//   -DWITNESS_TEXT   assert no accepted frame ever carries non-empty text -> must fail (accept path reachable)
#include "prelude.h"
#include <assert.h>
#include <aprs_functions.cpp>
#include <nrf52/WisBlock-API.h>

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
#ifndef REAL_CHARSET
// Contract model of the CHR-01 filter (src/charset_filter.cpp), used for the full-length job because the real UTF-8 decoder
// multiplies the formula size: result never exceeds the input length, buffer content stays arbitrary. The contract itself is
// proved separately (charset_contract.check); pass_realcs.check runs decodeAPRS with the real filter at a smaller bound.
size_t charset_filter_apply(char *buf, size_t len, charset_filter_mode mode)
{
    (void)buf; (void)mode;
    size_t n = (size_t)nondet_int();
    __ESBMC_assume(n <= len);
    return n;
}
#endif

// Models (over-approximation, either answer possible): the regex filter. Its own safety is not covered here (see README).
bool isSpecialTargetCall(const char *callsign) { (void)callsign; return nondet_int() != 0; }
bool checkRegexCall(String callsign) { (void)callsign; return nondet_int() != 0; }
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

#ifndef MAXLEN
#define MAXLEN 255
#endif

static bool nul_within(const char *s, unsigned cap)
{
    for(unsigned i = 0; i < cap; i++)
        if(s[i] == 0) return true;
    return false;
}

int main()
{
    int len = nondet_int();
    __ESBMC_assume(len >= 0 && len <= MAXLEN);
    // callers pass a UDP_TX_BUF_SIZE array (the declared parameter size); a shorter allocation is the mutation
#ifdef MUT_SHORT_BUF
    __ESBMC_assume(len >= 16);   // past the rsize<16 gate: the missing last byte can only be hit by a deep read
    uint8_t *buf = new uint8_t[len - 1];
    for(int i = 0; i < len - 1; i++) buf[i] = nondet_uchar();
#else
    uint8_t *buf = new uint8_t[len > 0 ? len : 1];
    for(int i = 0; i < len; i++) buf[i] = nondet_uchar();
#endif
    struct aprsMessage m;
    uint16_t r = decodeAPRS(buf, (uint16_t)len, m);
    if(r != 0)
    {
        assert(nul_within(m.msg_source_path, sizeof(m.msg_source_path)));
        assert(nul_within(m.msg_source_call, sizeof(m.msg_source_call)));
        assert(nul_within(m.msg_source_last, sizeof(m.msg_source_last)));
        assert(nul_within(m.msg_destination_path, sizeof(m.msg_destination_path)));
        assert(nul_within(m.msg_destination_call, sizeof(m.msg_destination_call)));
        assert(nul_within(m.msg_payload, sizeof(m.msg_payload)));
        assert(m.msg_len <= len);
    }
#ifdef WITNESS_TEXT
    assert(!(r != 0 && m.msg_payload[0] != 0));
#endif
    delete[] buf;
    return 0;
}
