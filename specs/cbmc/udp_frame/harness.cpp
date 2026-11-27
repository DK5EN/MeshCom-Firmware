// FORMAL-02 ESBMC harness: udpFrameZeroRun() / udpFrameClassify() (src/udp_frame_parse.h), the pure parse core of
// handleUdpFrame(). Surface: UDP datagram from the server, untrusted.
// Contract: buf holds exactly len bytes (any read at or past len is an out-of-bounds error), content fully nondet,
// len 0..LMAX (LMAX <= UDP_TX_BUF_SIZE = 255). Expected values are re-computed independently below.
// Modes:
//   (default)      memory safety + class / type / span / size / zero-run consistency
//   -DWITNESS_x    asserts the interesting verdict is NEVER produced -> must fail: REJECT, GATE, CONF, BEAT, OTHER
//   mutations      run with a mutated copy of udp_frame_parse.h in gen/ (see the .check files)
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <udp_frame_parse.h>

#ifndef LMAX
#define LMAX 12
#endif

extern "C" unsigned char nondet_uchar();
extern "C" unsigned int nondet_uint();

int main()
{
    size_t len = nondet_uint();
    __ESBMC_assume(len <= LMAX);
    uint8_t *buf = (uint8_t *)malloc(len);   // exactly len bytes; len 0 gives a zero-byte object
    __ESBMC_assume(buf != NULL);
    for (size_t i = 0; i < len; i++)
        buf[i] = nondet_uchar();

    UdpFrameInfo r;
    udpFrameClassify(buf, len, &r);

#if defined(WITNESS_REJECT)
    assert(r.cls != UDPF_REJECT_ZEROS);
#elif defined(WITNESS_GATE)
    assert(r.cls != UDPF_GATE);
#elif defined(WITNESS_CONF)
    assert(r.cls != UDPF_CONF);
#elif defined(WITNESS_BEAT)
    assert(r.cls != UDPF_BEAT);
#elif defined(WITNESS_OTHER)
    assert(r.cls != UDPF_OTHER);
#else
    // independent zero run: aligned pairs (0,1) (2,3) ... below len, counted back from the last pair
    size_t npairs = len / 2;
    size_t k = 0;
    while (k < npairs && buf[2 * (npairs - 1 - k)] == 0 && buf[2 * (npairs - 1 - k) + 1] == 0)
        k++;
    assert(r.zero_run == 2 * k);
    assert(udpFrameZeroRun(buf, len) == r.zero_run);

    // independent class
    bool four = len >= 4;
    bool g = four && buf[0] == 'G' && buf[1] == 'A' && buf[2] == 'T' && buf[3] == 'E';
    bool c = four && buf[0] == 'C' && buf[1] == 'O' && buf[2] == 'N' && buf[3] == 'F';
    bool b = four && buf[0] == 'B' && buf[1] == 'E' && buf[2] == 'A' && buf[3] == 'T';

    if (2 * k > MAX_ZEROS)
    {
        assert(r.cls == UDPF_REJECT_ZEROS);
        assert(r.gate_kind == UDPG_NONE && r.msg_type == 0 && r.conf_size_ok == 0 && r.gate_len == 0);
    }
    else if (g)
    {
        assert(r.cls == UDPF_GATE);
        assert(r.gate_len == len - 4);
        assert(r.conf_size_ok == 0);
        if (len == 4)
        {
            assert(r.gate_kind == UDPG_NONE && r.msg_type == 0);   // no type byte
        }
        else
        {
            assert(r.msg_type == buf[4]);
            if (buf[4] == 0x3A) assert(r.gate_kind == UDPG_TEXT);
            else if (buf[4] == 0x21) assert(r.gate_kind == UDPG_POS);
            else if (buf[4] == 0x40) assert(r.gate_kind == UDPG_HEY);
            else assert(r.gate_kind == UDPG_UNKNOWN);
        }
    }
    else if (c)
    {
        assert(r.cls == UDPF_CONF);
        assert(r.conf_size_ok == 1);   // 4 <= len <= 255 = UDP_CONF_BUFF_SIZE
        assert(r.gate_kind == UDPG_NONE && r.msg_type == 0 && r.gate_len == 0);
    }
    else if (b)
    {
        assert(r.cls == UDPF_BEAT);
        assert(r.gate_kind == UDPG_NONE && r.msg_type == 0 && r.conf_size_ok == 0 && r.gate_len == 0);
    }
    else
    {
        assert(r.cls == UDPF_OTHER);
        assert(r.gate_kind == UDPG_NONE && r.msg_type == 0 && r.conf_size_ok == 0 && r.gate_len == 0);
    }
#endif
    free(buf);
    return 0;
}
