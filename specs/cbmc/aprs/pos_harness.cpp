// FORMAL-01 ESBMC harness: decodeAPRSPOS(const char*, aprsPosition&) (src/aprs_functions.cpp) over every NUL-terminated payload
// up to MAXLEN characters. Callers pass aprsMessage.msg_payload (char[256]), so strlen <= 255 (UDP_TX_BUF_SIZE).
// Proves: no out-of-bounds access of the 259-byte stack copy, decode_text[25], cregion[48], decode_grc[40], ctoken[8].
// sscanf: esbmc models it; String is the bounded stub in stubs/Arduino.h.
// Mutations: -DMUT_SHORT_BUF (payload array one byte shorter than strlen+1 -> OOB read), -DWITNESS_POS (assert never
// accepted -> must fail).
#include "prelude.h"
#include <assert.h>
#include <aprs_functions.cpp>
#include <nrf52/WisBlock-API.h>

s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
bool isSpecialTargetCall(const char *callsign) { (void)callsign; return false; }
bool checkRegexCall(String callsign) { (void)callsign; return true; }
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

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
    char *p = new char[len];
#else
    char *p = new char[len + 1];
#endif
    for(int i = 0; i < len; i++)
    {
        p[i] = (char)nondet_uchar();
        __ESBMC_assume(p[i] != 0);
    }
#ifndef MUT_SHORT_BUF
    p[len] = 0;
#endif
    struct aprsPosition pos;
    uint16_t r = decodeAPRSPOS(p, pos);
    if(r == 1)
    {
        assert(pos.grccnt >= 0 && pos.grccnt <= 6);
        assert(pos.din[8] == 0);
    }
#ifdef WITNESS_POS
    assert(r != 1);
#endif
    delete[] p;
    return 0;
}
