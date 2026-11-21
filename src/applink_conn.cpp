// AppLink connection engine, see applink_conn.h.
#include "applink_conn.h"
#include <stdlib.h>   // malloc/free: handshake scratch
#include <string.h>

static void alFinish(AlConn *c, uint16_t code)
{
    // wasAuthed = the glue was told about the session. ALC_AUTHED alone is not enough: the state is
    // set before the "ok" flush, and a link that dies in that flush never reached onAuthed.
    bool was = c->announced;
    c->announced = false;
    c->state = ALC_IDLE;
    c->st.last_close = code;
    c->io.close(c->io.ctx);
    c->io.onClosed(c->io.ctx, was);
}

// false = connection dead (io.write returned -1)
static bool alFlush(AlConn *c)
{
    while (c->out_off < c->out_len)
    {
        int n = c->io.write(c->io.ctx, c->out + c->out_off, (size_t)(c->out_len - c->out_off));
        if (n < 0)
            return false;
        if (n == 0)
            break;
        size_t rem = (size_t)(c->out_len - c->out_off);
        c->out_off = (uint16_t)(c->out_off + ((size_t)n > rem ? rem : (size_t)n));
    }
    if (c->out_off >= c->out_len)
        c->out_off = c->out_len = 0;
    return true;
}

static size_t alFree(const AlConn *c)
{
    return APPLINK_OUT_BUF - (size_t)(c->out_len - c->out_off);
}

// make room at the tail by dropping the already sent head
static void alCompact(AlConn *c)
{
    if (c->out_off)
    {
        memmove(c->out, c->out + c->out_off, (size_t)(c->out_len - c->out_off));
        c->out_len = (uint16_t)(c->out_len - c->out_off);
        c->out_off = 0;
    }
}

// queue one complete encoded frame; false if it does not fit
static bool alQueueFrame(AlConn *c, uint8_t op, const uint8_t *payload, size_t len)
{
    alCompact(c);
    size_t n = alWsEncode(op, payload, len, c->out + c->out_len, APPLINK_OUT_BUF - c->out_len);
    if (!n)
        return false;
    c->out_len = (uint16_t)(c->out_len + n);
    return true;
}

static bool alQueueText(AlConn *c, const char *json, size_t len)
{
    return len && alQueueFrame(c, AL_OP_TEXT, (const uint8_t *)json, len);
}

static void alDead(AlConn *c)
{
    alFinish(c, 1006);
}

// flush, queue pending pong/hb that fit, flush again; handles a dead connection
static void alPump(AlConn *c)
{
    if (c->state == ALC_IDLE)
        return;
    if (!alFlush(c))
        return alDead(c);
    if (c->pend_pong && alQueueFrame(c, AL_OP_PONG, c->pong, c->pong_len))
        c->pend_pong = false;
    if (c->pend_hb)
    {
        char t[16];
        size_t n = alHbJson(t, sizeof(t));
        if (alQueueText(c, t, n))
            c->pend_hb = false;
    }
    if (!alFlush(c))
        alDead(c);
}

void alConnInit(AlConn *c, const AlConnIo *io)
{
    memset(c, 0, sizeof(*c));
    c->io = *io;
    c->state = ALC_IDLE;
}

void alConnOpen(AlConn *c, const uint8_t nonce[APPLINK_NONCE_LEN], uint32_t now_ms)
{
    memcpy(c->nonce, nonce, APPLINK_NONCE_LEN);
    c->state = ALC_HTTP;
    c->open_ms = c->last_rx = c->hb_ms = now_ms;
    c->out_off = c->out_len = 0;
    c->http_len = 0;
    c->pend_hb = c->pend_pong = false;
    c->announced = false;
    c->pong_len = 0;
}

bool alConnAuthed(const AlConn *c)
{
    return c->state == ALC_AUTHED;
}

bool alConnActive(const AlConn *c)
{
    return c->state != ALC_IDLE;
}

void alConnClose(AlConn *c, uint16_t code, const char *reason)
{
    if (c->state == ALC_IDLE)
        return;
    // RFC 6455 7.4.1: 1005/1006/1015 are status values, never sent in a close
    // frame. The glue uses 1006 for a dead socket: no bye, no close frame.
    const bool wireCode = code != 1005 && code != 1006 && code != 1015;
    if (c->state >= ALC_CHAL && wireCode)
    {
        bool alive = alFlush(c);
        if (alive)
        {
            if (c->state == ALC_AUTHED && reason && *reason)
            {
                char t[192];
                size_t n = alByeJson(reason, t, sizeof(t));
                alQueueText(c, t, n);
            }
            alCompact(c);
            size_t n = alWsEncodeClose(code, NULL, c->out + c->out_len, APPLINK_OUT_BUF - c->out_len);
            c->out_len = (uint16_t)(c->out_len + n);
            alFlush(c);  // best effort
        }
    }
    alFinish(c, code);
}

static bool alPasswordEmpty(const char *p)
{
    if (!p)
        return true;
    while (*p)
        if (*p++ != ' ')
            return false;
    return true;
}

static void alHandleAuth(AlConn *c, const uint8_t mac[32], const AlConnInfo *info, AlLockout *lock, uint32_t now)
{
    char t[48];
    if (lock && alLockoutActive(lock, now))
    {
        alQueueText(c, t, alAuthErrJson("locked", t, sizeof(t)));
        alConnClose(c, 4429, NULL);
        return;
    }
    if (!info || alPasswordEmpty(info->password))
    {
        alQueueText(c, t, alAuthErrJson("disabled", t, sizeof(t)));
        alConnClose(c, 4403, NULL);
        return;
    }
    if (alAuthCheck(mac, info->password, c->nonce))
    {
        if (lock)
            alLockoutSuccess(lock);
        alQueueText(c, t, alAuthOkJson(APPLINK_MAX_OUT, t, sizeof(t)));
        c->state = ALC_AUTHED;
        c->hb_ms = c->last_rx = now;
        alPump(c);
        if (c->state == ALC_AUTHED)
        {
            c->announced = true;
            c->io.onAuthed(c->io.ctx);
        }
        return;
    }
    if (lock)
        alLockoutFail(lock, now);
    c->st.auth_fail++;
    alQueueText(c, t, alAuthErrJson("auth", t, sizeof(t)));
    alConnClose(c, 4401, NULL);
}

static void alWsRx(AlConn *c, const uint8_t *buf, size_t len, const AlConnInfo *info, AlLockout *lock, uint32_t now)
{
    AlWsDecoder *d = &c->u.dec;
    while (len > 0 && c->state >= ALC_CHAL)
    {
        AlWsEvent ev = AL_WS_NONE;
        size_t used = alWsFeed(d, buf, len, &ev);
        buf += used;
        len -= used;
        switch (ev)
        {
        case AL_WS_ERROR:
            alConnClose(c, d->error_code, NULL);
            return;
        case AL_WS_TEXT:
            if (c->state == ALC_CHAL)
            {
                uint8_t mac[32];
                if (alCtlParse((const char *)d->msg, d->msg_len, mac) == AL_CTL_AUTH)
                    alHandleAuth(c, mac, info, lock, now);
            }
            break;
        case AL_WS_BINARY:
            if (c->state == ALC_CHAL)
                alConnClose(c, 4401, NULL);
            else
            {
                c->st.frames_in++;
                c->io.onFrame(c->io.ctx, d->msg, d->msg_len);
            }
            break;
        case AL_WS_PING:
            memcpy(c->pong, d->ctl, d->ctl_len);
            c->pong_len = (uint8_t)d->ctl_len;
            c->pend_pong = true;
            break;
        case AL_WS_CLOSE:
            alConnClose(c, d->close_code == 1005 ? 1000 : d->close_code, NULL);
            return;
        default:
            break;
        }
        if (used == 0 && ev == AL_WS_NONE)
            break;
    }
}

static void alHttpRx(AlConn *c, const uint8_t *buf, size_t len, const AlConnInfo *info, AlLockout *lock, uint32_t now)
{
    size_t space = APPLINK_HTTP_MAX - c->http_len;
    size_t n = len < space ? len : space;
    memcpy(c->u.http + c->http_len, buf, n);
    c->http_len = (uint16_t)(c->http_len + n);

    AlHttpReq req;
    AlHttpResult r = alHttpParse(c->u.http, c->http_len, &req);
    if (r == AL_HTTP_NEED_MORE)
        return;
    // RFC 6455 4.4: a wrong version gets 426 + Sec-WebSocket-Version, anything else 400
    const int status = (r == AL_HTTP_OK) ? alHttpStatusFor(&req) : 400;
    if (status != 0)
    {
        char t[160];
        size_t m = alHttpReject(status, t, sizeof(t));
        if (m)
            c->io.write(c->io.ctx, (const uint8_t *)t, m);  // best effort
        alFinish(c, (uint16_t)status);
        return;
    }
    size_t end = 0;
    while (end + 4 <= c->http_len && memcmp(c->u.http + end, "\r\n\r\n", 4) != 0)
        end++;
    end += 4;
    // Handshake scratch: not on the stack (the nRF52 loop task has 4 KB in total) and not static either
    // (1.25 KB of RAM on every node that never uses AppLink, T-Beam idle-heap gate 2026-11-21). One heap
    // block per handshake, freed before this returns.
    struct Scratch
    {
        uint8_t tail[APPLINK_HTTP_MAX];
        char json[256];
    };
    Scratch *sc = (Scratch *)malloc(sizeof(Scratch));
    if (!sc)
        return alFinish(c, 1011);
    size_t tail_len = c->http_len > end ? c->http_len - end : 0;
    if (tail_len)
        memcpy(sc->tail, c->u.http + end, tail_len);

    c->out_off = c->out_len = 0;
    size_t a = alHttpAccept(&req, (char *)c->out, APPLINK_OUT_BUF);
    size_t jl = a ? alAuthChallengeJson(c->nonce, info ? info->call : "", info ? info->fw : "", info ? info->hw : "", sc->json, sizeof(sc->json)) : 0;
    bool ok = a && jl;
    if (ok)
    {
        c->out_len = (uint16_t)a;
        ok = alQueueText(c, sc->json, jl);
    }
    if (!ok)
    {
        free(sc);
        return alFinish(c, 1011);
    }
    alWsDecoderInit(&c->u.dec);  // overwrites the HTTP buffer, which is consumed
    c->state = ALC_CHAL;
    alPump(c);
    if (c->state != ALC_IDLE && tail_len)
        alWsRx(c, sc->tail, tail_len, info, lock, now);
    free(sc);
    if (c->state == ALC_IDLE)
        return;
    if (n < len && c->state >= ALC_CHAL)
        alWsRx(c, buf + n, len - n, info, lock, now);
}

void alConnRx(AlConn *c, const uint8_t *buf, size_t len, const AlConnInfo *info, AlLockout *lock, uint32_t now_ms)
{
    if (c->state == ALC_IDLE || !len)
        return;
    c->last_rx = now_ms;
    if (c->state == ALC_HTTP)
        alHttpRx(c, buf, len, info, lock, now_ms);
    else
        alWsRx(c, buf, len, info, lock, now_ms);
    alPump(c);
}

void alConnPoll(AlConn *c, uint32_t now_ms)
{
    if (c->state == ALC_IDLE)
        return;
    if (c->state != ALC_AUTHED)
    {
        if ((uint32_t)(now_ms - c->open_ms) >= APPLINK_LOGIN_TIMEOUT_MS)
            return alConnClose(c, 4408, NULL);
    }
    else
    {
        if ((uint32_t)(now_ms - c->last_rx) >= APPLINK_SILENCE_MS)
            return alConnClose(c, 4408, NULL);
        if ((uint32_t)(now_ms - c->hb_ms) >= APPLINK_HB_INTERVAL_MS)
        {
            c->pend_hb = true;
            c->hb_ms = now_ms;
        }
    }
    alPump(c);
}

BlePhoneSend alConnSendFrame(AlConn *c, const uint8_t *frame, uint16_t len, uint32_t now_ms)
{
    (void)now_ms;
    if (c->state != ALC_AUTHED)
        return BLE_SEND_DOWN;
    if (!alFlush(c))
    {
        alDead(c);
        return BLE_SEND_DOWN;
    }
    size_t enc = len + (len < 126 ? 2 : 4);
    if (enc > alFree(c))
    {
        c->st.busy++;
        return BLE_SEND_BUSY;
    }
    if (!alQueueFrame(c, AL_OP_BIN, frame, len))
    {
        c->st.busy++;
        return BLE_SEND_BUSY;
    }
    c->st.frames_out++;
    if (!alFlush(c))
        alDead(c);  // frame counted as sent; connection is gone now
    return BLE_SEND_SENT;
}
