// FORMAL-01 ESBMC harness: encodeStartAPRS() (src/aprs_functions.cpp) vs the 255-byte (UDP_TX_BUF_SIZE) target (docs F3-8).
// Inputs: any NUL-terminated source/destination path (up to the struct field width) and any payload_type/msg_id/flags.
// Proves: no write past msg_buffer[UDP_TX_BUF_SIZE], no read past msg_start / the path fields; returned length <= 255.
// Mutations / defect:
//   -DMUT_SHORT_BUF   target one byte short of what the worst case writes -> write past the end must be found
//   enc_pass_widepath.check / enc_mut_widepath.check (-DMUT_F3_8) build a copy of src/aprs_structures.h in gen/ with MC_PATH_LEN 121 -> 140 (sed at job time) and puts gen/
//                     first on the include path: with path fields wider than today the clamp "ilng >= 255 -> 254" forgets
//                     the 6-byte header offset and the memcpy writes msg_buffer[6..259] into a 255-byte target = F3-8.
//                     Unreachable at the current width (121 -> ilng <= 242); latent.
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

extern "C" int nondet_int();
extern "C" unsigned char nondet_uchar();

static void fill_nul_terminated(char *f, unsigned cap)
{
    // any content with a NUL somewhere inside the field (the contract of every strlen() consumer of these fields)
    unsigned n = (unsigned)nondet_int();
    __ESBMC_assume(n < cap);
    for(unsigned i = 0; i < n; i++)
    {
        f[i] = (char)nondet_uchar();
        __ESBMC_assume(f[i] != 0);
    }
    f[n] = 0;
    for(unsigned i = n + 1; i < cap; i++) f[i] = (char)nondet_uchar();
}

int main()
{
    struct aprsMessage m;
    fill_nul_terminated(m.msg_source_path, sizeof(m.msg_source_path));
    fill_nul_terminated(m.msg_destination_path, sizeof(m.msg_destination_path));
    m.payload_type = (char)nondet_uchar();
    m.msg_id = (unsigned)nondet_int();
    m.max_hop = nondet_uchar();
    m.msg_server = nondet_int() != 0;
    m.msg_track = nondet_int() != 0;
    m.msg_app_offline = nondet_int() != 0;
#ifdef MUT_SHORT_BUF
    // worst case written index is 6+ilng-1; with both paths at full width that is 6+2*120+1... use one byte less than needed
    uint8_t *buf = new uint8_t[6 + strlen(m.msg_source_path) + 1 + strlen(m.msg_destination_path) + 1 - 1];
#else
    uint8_t *buf = new uint8_t[UDP_TX_BUF_SIZE];
#endif
    uint16_t r = encodeStartAPRS(buf, m);
    assert(r <= UDP_TX_BUF_SIZE);
    delete[] buf;
    return 0;
}
