// Reference for test_applink_ws_range: the frame decoder of src/applink_ws.cpp as it stood before B-AL-RANGE-01
// (fork-dev cdfc76ea), verbatim from the phase enum to the end of alWsFeed(), renamed alref::feed(). The new decoder
// must leave the same decoder state, events and consumed counts on every input.
#ifndef APPLINK_WS_REF_H
#define APPLINK_WS_REF_H
#include <stdint.h>
#include <string.h>
#include <applink_ws.h>

namespace alref
{
enum
{
    PH_B0 = 0,
    PH_B1,
    PH_EXT,
    PH_MASK,
    PH_PAYLOAD,
    PH_ERROR
};

static AlWsEvent fail(AlWsDecoder *d, uint16_t code)
{
    d->phase = PH_ERROR;
    d->error_code = code;
    return AL_WS_ERROR;
}

// header complete (length and mask known): validate size, enter payload or finish an empty frame
static AlWsEvent headerDone(AlWsDecoder *d)
{
    const bool ctl = (d->opcode & 0x8) != 0;
    if (!ctl && d->ext_len + d->msg_len > APPLINK_MAX_IN)
        return fail(d, 1009);
    d->remaining = d->ext_len;
    d->mask_pos = 0;
    d->phase = PH_PAYLOAD;
    return AL_WS_NONE;
}

// frame complete: report it
static AlWsEvent frameDone(AlWsDecoder *d)
{
    d->phase = PH_B0;
    switch (d->opcode)
    {
    case AL_OP_PING:
        d->done = 2;
        return AL_WS_PING;
    case AL_OP_PONG:
        d->done = 2;
        return AL_WS_PONG;
    case AL_OP_CLOSE:
        d->done = 2;
        if (d->ctl_len == 1)
            return fail(d, 1002);
        d->close_code = d->ctl_len >= 2 ? (uint16_t)((d->ctl[0] << 8) | d->ctl[1]) : 1005;
        if (d->ctl_len >= 2)
        {
            // RFC 6455 7.4: 1004-1006 and 1015 are reserved, 1016-2999 unassigned, >= 5000 undefined
            const uint16_t c = d->close_code;
            if (c < 1000 || c == 1004 || c == 1005 || c == 1006 || (c >= 1015 && c <= 2999) || c >= 5000)
                return fail(d, 1002);
        }
        return AL_WS_CLOSE;
    default:
        break;
    }
    if (!d->fin)
        return AL_WS_NONE;  // more fragments to come
    const uint8_t op = d->frag_op;
    d->frag_op = 0;
    d->done = 1;
    return op == AL_OP_TEXT ? AL_WS_TEXT : AL_WS_BINARY;
}

size_t feed(AlWsDecoder *d, const uint8_t *in, size_t len, AlWsEvent *ev)
{
    AlWsEvent e = AL_WS_NONE;
    size_t i = 0;
    if (ev)
        *ev = AL_WS_NONE;
    if (!d || (len && !in))
        return 0;
    if (d->phase == PH_ERROR)
    {
        if (ev)
            *ev = AL_WS_ERROR;
        return 0;
    }
    if (d->done == 1)
        d->msg_len = 0;
    else if (d->done == 2)
        d->ctl_len = 0;
    d->done = 0;

    while (i < len && e == AL_WS_NONE)
    {
        switch (d->phase)
        {
        case PH_B0:
        {
            const uint8_t b = in[i++];
            const uint8_t op = b & 0x0F;
            d->fin = (b & 0x80) != 0;
            d->opcode = op;
            if (b & 0x70)
            {
                e = fail(d, 1002);
                break;
            }
            if (op == AL_OP_CONT)
            {
                if (!d->frag_op)
                    e = fail(d, 1002);
            }
            else if (op == AL_OP_TEXT || op == AL_OP_BIN)
            {
                if (d->frag_op)
                    e = fail(d, 1002);
                else
                {
                    d->frag_op = op;
                    d->msg_len = 0;
                }
            }
            else if (op == AL_OP_CLOSE || op == AL_OP_PING || op == AL_OP_PONG)
            {
                if (!d->fin)
                    e = fail(d, 1002);
                else
                    d->ctl_len = 0;
            }
            else
                e = fail(d, 1002);
            if (e == AL_WS_NONE)
                d->phase = PH_B1;
            break;
        }
        case PH_B1:
        {
            const uint8_t b = in[i++];
            if (!(b & 0x80))
            {
                e = fail(d, 1002);
                break;
            }
            const uint8_t l7 = b & 0x7F;
            const bool ctl = (d->opcode & 0x8) != 0;
            d->ext_len = 0;
            d->mask_got = 0;
            if (l7 >= 126)
            {
                if (ctl)
                {
                    e = fail(d, 1002);
                    break;
                }
                d->ext_need = (l7 == 126) ? 2 : 8;
                d->phase = PH_EXT;
            }
            else
            {
                d->ext_len = l7;
                d->phase = PH_MASK;
            }
            break;
        }
        case PH_EXT:
            d->ext_len = (d->ext_len << 8) | in[i++];
            if (--d->ext_need == 0)
            {
                if (d->ext_len > APPLINK_MAX_IN)  // also catches 64-bit lengths (data frames only here)
                    e = fail(d, 1009);
                else
                    d->phase = PH_MASK;
            }
            break;
        case PH_MASK:
            d->mask[d->mask_got++] = in[i++];
            if (d->mask_got == 4)
            {
                e = headerDone(d);
                if (e == AL_WS_NONE && d->remaining == 0)
                    e = frameDone(d);
            }
            break;
        case PH_PAYLOAD:
        {
            size_t n = len - i;
            if ((uint64_t)n > d->remaining)
                n = (size_t)d->remaining;
            const bool ctl = (d->opcode & 0x8) != 0;
            uint8_t *dst = ctl ? d->ctl + d->ctl_len : d->msg + d->msg_len;
            for (size_t k = 0; k < n; k++)
                dst[k] = (uint8_t)(in[i + k] ^ d->mask[(d->mask_pos + k) & 3]);
            d->mask_pos += n;
            if (ctl)
                d->ctl_len += n;
            else
                d->msg_len += n;
            d->remaining -= n;
            i += n;
            if (d->remaining == 0)
                e = frameDone(d);
            break;
        }
        default:
            return i;
        }
    }
    if (ev)
        *ev = e;
    return i;
}
} // namespace alref
#endif
