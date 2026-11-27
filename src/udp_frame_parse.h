#pragma once

// Pure parse core of the gateway UDP receive handlers (FORMAL-02, docs/
// udp-consolidation-20261127.md section 5.3). Header-only, no Arduino, no
// String, no IPAddress, no globals: an explicit buffer plus length in, a small
// struct out, so a bounded model checker can take it without the rest of the
// handler. handleUdpFrame() (src/udp_frame.cpp) calls it for the zero scan,
// the indicator class and the GATE payload span, so those decisions exist once.
//
// Precondition for both functions: len <= UDP_TX_BUF_SIZE (callers bound it:
// ESP32 reads at most UDP_TX_BUF_SIZE-1, nRF52 accepts packetSize <=
// UDP_TX_BUF_SIZE) and buf points to a slab of at least UDP_TX_BUF_SIZE bytes.

#include <stdint.h>
#include <stddef.h>
#include "configuration_global.h"   // UDP_TX_BUF_SIZE, UDP_CONF_BUFF_SIZE, UDP_MSG_INDICATOR_LEN, MAX_ZEROS

// D5 (operator decision 2026-11-27): the verdict depends on the first len bytes
// only, never on what the slab holds behind them. A datagram under 4 bytes is
// class OTHER, and a 4-byte GATE carries no type (gate_kind UDPG_NONE,
// msg_type 0); buf[4] is not read when len == 4. Before, the four indicator
// bytes and buf[4] were read whatever len was and the callers' NUL wipe was
// the only thing keeping a short datagram from matching.

enum UdpFrameClass { UDPF_REJECT_ZEROS = 0, UDPF_GATE, UDPF_CONF, UDPF_BEAT, UDPF_OTHER };
enum UdpGateKind   { UDPG_NONE = 0, UDPG_TEXT /*0x3A*/, UDPG_POS /*0x21*/, UDPG_HEY /*0x40*/, UDPG_UNKNOWN };

struct UdpFrameInfo
{
    uint8_t  cls;           // UdpFrameClass
    uint8_t  gate_kind;     // UdpGateKind; UDPG_NONE unless cls == UDPF_GATE
    uint8_t  msg_type;      // buf[UDP_MSG_INDICATOR_LEN] for a GATE frame, else 0
    uint8_t  conf_size_ok;  // 1 iff cls == UDPF_CONF and UDP_MSG_INDICATOR_LEN <= len <= UDP_CONF_BUFF_SIZE
    uint16_t gate_len;      // payload bytes after the indicator, clamped to UDP_TX_BUF_SIZE; 0 unless GATE
    uint8_t  zero_run;      // trailing run of aligned zero pairs, in bytes (the old zerocount)
};

// Number of zero bytes in the trailing run of aligned (even-index) zero
// pairs; an odd last byte is not scanned. uint8_t like the old counter: for
// len <= 255 it peaks at 254 and cannot wrap. The loop bound is the constant
// slab size, so the scan never reads past buf[UDP_TX_BUF_SIZE-1].
static inline uint8_t udpFrameZeroRun(const uint8_t *buf, size_t len)
{
    uint8_t zerocount = 0;

    for (size_t i = 0; i + 1 < len && i + 1 < UDP_TX_BUF_SIZE; i += 2)
    {
        if (buf[i] == 0x00 && buf[i + 1] == 0x00)
            zerocount += 2;
        else
            zerocount = 0;
    }
    return zerocount;
}

static inline void udpFrameClassify(const uint8_t *buf, size_t len, UdpFrameInfo *out)
{
    out->cls = UDPF_OTHER;
    out->gate_kind = UDPG_NONE;
    out->msg_type = 0;
    out->conf_size_ok = 0;
    out->gate_len = 0;
    out->zero_run = udpFrameZeroRun(buf, len);

    if (out->zero_run > MAX_ZEROS)
    {
        out->cls = UDPF_REJECT_ZEROS;
        return;
    }

    if (len < UDP_MSG_INDICATOR_LEN)
        return;   // D5: class OTHER

    const bool isGate = buf[0] == 'G' && buf[1] == 'A' && buf[2] == 'T' && buf[3] == 'E';
    const bool isConf = buf[0] == 'C' && buf[1] == 'O' && buf[2] == 'N' && buf[3] == 'F';
    const bool isBeat = buf[0] == 'B' && buf[1] == 'E' && buf[2] == 'A' && buf[3] == 'T';

    if (isGate)
    {
        out->cls = UDPF_GATE;

        uint16_t gl = (uint16_t)(len - UDP_MSG_INDICATOR_LEN);   // len >= 4 here (D5)
        if (gl > UDP_TX_BUF_SIZE)
            gl = UDP_TX_BUF_SIZE;   // for safety; unreachable for len <= UDP_TX_BUF_SIZE
        out->gate_len = gl;

        if (out->gate_len == 0)
            return;   // D5: 4-byte GATE, no type byte, gate_kind stays UDPG_NONE
        out->msg_type = buf[UDP_MSG_INDICATOR_LEN];
        switch (out->msg_type)
        {
            case 0x3A: out->gate_kind = UDPG_TEXT;    break;
            case 0x21: out->gate_kind = UDPG_POS;     break;
            case 0x40: out->gate_kind = UDPG_HEY;     break;
            default:   out->gate_kind = UDPG_UNKNOWN; break;
        }
    }
    else if (isConf)
    {
        out->cls = UDPF_CONF;
        out->conf_size_ok = (len >= UDP_MSG_INDICATOR_LEN && len <= UDP_CONF_BUFF_SIZE) ? 1 : 0;
    }
    else if (isBeat)
    {
        out->cls = UDPF_BEAT;
    }
}
