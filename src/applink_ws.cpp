#include "applink_ws.h"
#include "sha1_base64.h"
#include <string.h>

// ---------------------------------------------------------------- HTTP

static char lc(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// case-insensitive compare of [p, p+n) with the NUL-terminated literal
static bool ieq(const char *p, size_t n, const char *lit)
{
    if (strlen(lit) != n)
        return false;
    for (size_t i = 0; i < n; i++)
        if (lc(p[i]) != lc(lit[i]))
            return false;
    return true;
}

// does the comma separated list [p, p+n) contain the token (surrounding blanks ignored)?
static bool listHas(const char *p, size_t n, const char *tok, bool ci)
{
    const size_t tl = strlen(tok);
    size_t i = 0;
    while (i <= n)
    {
        size_t j = i;
        while (j < n && p[j] != ',')
            j++;
        size_t a = i, b = j;
        while (a < b && (p[a] == ' ' || p[a] == '\t'))
            a++;
        while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t'))
            b--;
        if (b - a == tl)
        {
            bool same = true;
            for (size_t k = 0; k < tl; k++)
            {
                char x = p[a + k], y = tok[k];
                if (ci ? lc(x) != lc(y) : x != y)
                {
                    same = false;
                    break;
                }
            }
            if (same)
                return true;
        }
        i = j + 1;
    }
    return false;
}

// Sec-WebSocket-Key is the base64 of a 16 byte nonce: 22 base64 chars + "==" (RFC 6455 4.2.1)
static bool keyShapeOk(const char *v, size_t n)
{
    if (n != 24 || v[22] != '=' || v[23] != '=')
        return false;
    for (size_t i = 0; i < 22; i++)
    {
        const char c = v[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/'))
            return false;
    }
    return true;
}

AlHttpResult alHttpParse(const char *buf, size_t len, AlHttpReq *out)
{
    if (!buf || !out)
        return AL_HTTP_BAD;
    size_t end = 0;
    bool found = false;
    for (size_t i = 0; i + 4 <= len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n')
        {
            end = i;
            found = true;
            break;
        }
    if (!found)
        return len >= APPLINK_HTTP_MAX ? AL_HTTP_TOO_BIG : AL_HTTP_NEED_MORE;
    if (end + 4 > APPLINK_HTTP_MAX)
        return AL_HTTP_TOO_BIG;

    memset(out, 0, sizeof(*out));
    // request line: GET <target> HTTP/1.x
    size_t p = 0;
    if (end < 4 || memcmp(buf, "GET ", 4) != 0)
        return AL_HTTP_BAD;
    p = 4;
    size_t ts = p;
    while (p < end && buf[p] != ' ' && buf[p] != '\r')
        p++;
    if (p >= end || buf[p] != ' ')
        return AL_HTTP_BAD;
    size_t te = p;
    size_t pathLen = te - ts;
    for (size_t i = ts; i < te; i++)
        if (buf[i] == '?')
        {
            pathLen = i - ts;
            break;
        }
    out->path_ok = (pathLen == strlen(APPLINK_PATH) && memcmp(buf + ts, APPLINK_PATH, pathLen) == 0);
    p++;
    if (p + 8 > end || memcmp(buf + p, "HTTP/1.", 7) != 0 || buf[p + 7] < '0' || buf[p + 7] > '9')
        return AL_HTTP_BAD;
    p += 8;
    // the request line must end right here (CRLF follows, or the head is the line alone)
    if (p != end && !(buf[p] == '\r' && buf[p + 1] == '\n'))
        return AL_HTTP_BAD;

    bool haveKey = false, haveHost = false, upg = false, conn = false;
    while (p < end)
    {
        p += 2;  // skip CRLF
        size_t ls = p;
        while (p < end && !(buf[p] == '\r' && buf[p + 1] == '\n'))
            p++;
        size_t le = p;
        if (le == ls)
            continue;
        size_t c = ls;
        while (c < le && buf[c] != ':')
            c++;
        if (c >= le || c == ls)
            return AL_HTTP_BAD;
        size_t nl = c - ls;
        size_t vs = c + 1, ve = le;
        while (vs < ve && (buf[vs] == ' ' || buf[vs] == '\t'))
            vs++;
        while (ve > vs && (buf[ve - 1] == ' ' || buf[ve - 1] == '\t'))
            ve--;
        const char *name = buf + ls;
        const char *val = buf + vs;
        const size_t vl = ve - vs;
        if (ieq(name, nl, "upgrade"))
        {
            if (ieq(val, vl, "websocket"))
                upg = true;
        }
        else if (ieq(name, nl, "connection"))
        {
            if (listHas(val, vl, "upgrade", true))
                conn = true;
        }
        else if (ieq(name, nl, "sec-websocket-version"))
        {
            if (vl == 2 && val[0] == '1' && val[1] == '3')
                out->version_ok = true;
        }
        else if (ieq(name, nl, "sec-websocket-protocol"))
        {
            if (listHas(val, vl, APPLINK_SUBPROTO, false))
                out->proto_ok = true;
        }
        else if (ieq(name, nl, "host"))
        {
            if (vl > 0)
                haveHost = true;
        }
        else if (ieq(name, nl, "sec-websocket-key"))
        {
            if (!keyShapeOk(val, vl))
                return AL_HTTP_BAD;
            memcpy(out->key, val, vl);
            out->key[vl] = '\0';
            haveKey = true;
        }
    }
    if (!haveKey || !haveHost)
        return AL_HTTP_BAD;
    out->host_ok = true;
    out->upgrade_ok = upg && conn;
    return AL_HTTP_OK;
}

static bool appendStr(char *out, size_t out_size, size_t *pos, const char *s)
{
    const size_t n = strlen(s);
    if (*pos + n + 1 > out_size)
        return false;
    memcpy(out + *pos, s, n);
    *pos += n;
    out[*pos] = '\0';
    return true;
}

size_t alHttpAccept(const AlHttpReq *req, char *out, size_t out_size)
{
    if (!req || !out || out_size == 0)
        return 0;
    char acc[29];
    if (!wsAcceptKey(req->key, acc))
        return 0;
    size_t pos = 0;
    out[0] = '\0';
    if (appendStr(out, out_size, &pos, "HTTP/1.1 101 Switching Protocols\r\n"
                                       "Upgrade: websocket\r\n"
                                       "Connection: Upgrade\r\n"
                                       "Sec-WebSocket-Accept: ") &&
        appendStr(out, out_size, &pos, acc) &&
        appendStr(out, out_size, &pos, "\r\nSec-WebSocket-Protocol: " APPLINK_SUBPROTO "\r\n\r\n"))
        return pos;
    out[0] = '\0';
    return 0;
}

size_t alHttpReject(int status, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return 0;
    const char *line;
    const char *extra = "";
    if (status == 403)
        line = "HTTP/1.1 403 Forbidden\r\n";
    else if (status == 426)
    {
        line = "HTTP/1.1 426 Upgrade Required\r\n";
        extra = "Sec-WebSocket-Version: 13\r\n";
    }
    else if (status == 503)
        line = "HTTP/1.1 503 Service Unavailable\r\n";
    else
        line = "HTTP/1.1 400 Bad Request\r\n";
    size_t pos = 0;
    out[0] = '\0';
    if (appendStr(out, out_size, &pos, line) && appendStr(out, out_size, &pos, extra) &&
        appendStr(out, out_size, &pos, "Connection: close\r\nContent-Length: 0\r\n\r\n"))
        return pos;
    out[0] = '\0';
    return 0;
}

int alHttpStatusFor(const AlHttpReq *r)
{
    if (!r)
        return 400;
    if (r->path_ok && r->upgrade_ok && r->proto_ok && r->host_ok)
        return r->version_ok ? 0 : 426;
    return 400;
}

// ---------------------------------------------------------------- frame encoder

size_t alWsEncode(uint8_t opcode, const uint8_t *payload, size_t len, uint8_t *out, size_t out_size)
{
    if (!out || (len && !payload) || len > 65535 || (opcode & 0x8 && len > 125))
        return 0;
    const size_t hdr = len < 126 ? 2 : 4;
    if (out_size < hdr + len)
        return 0;
    out[0] = (uint8_t)(0x80 | (opcode & 0x0F));
    if (len < 126)
        out[1] = (uint8_t)len;
    else
    {
        out[1] = 126;
        out[2] = (uint8_t)(len >> 8);
        out[3] = (uint8_t)len;
    }
    if (len)
        memcpy(out + hdr, payload, len);
    return hdr + len;
}

size_t alWsEncodeClose(uint16_t code, const char *reason, uint8_t *out, size_t out_size)
{
    uint8_t pl[125];
    pl[0] = (uint8_t)(code >> 8);
    pl[1] = (uint8_t)code;
    size_t rl = reason ? strlen(reason) : 0;
    if (rl > 123)
        rl = 123;
    if (rl)
        memcpy(pl + 2, reason, rl);
    return alWsEncode(AL_OP_CLOSE, pl, 2 + rl, out, out_size);
}

// ---------------------------------------------------------------- frame decoder

enum
{
    PH_B0 = 0,
    PH_B1,
    PH_EXT,
    PH_MASK,
    PH_PAYLOAD,
    PH_ERROR
};

void alWsDecoderInit(AlWsDecoder *d)
{
    memset(d, 0, sizeof(*d));
    d->phase = PH_B0;
    d->close_code = 1005;
}

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

size_t alWsFeed(AlWsDecoder *d, const uint8_t *in, size_t len, AlWsEvent *ev)
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
