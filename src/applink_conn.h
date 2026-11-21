// AppLink connection engine (docs/applink-api-contract.md): one TCP connection's whole life --
// HTTP upgrade, challenge/login, heartbeat, timeouts, output buffering, close.
// Transport independent: the platform glue supplies AlConnIo. Arduino-free, no heap.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "applink_ws.h"
#include "applink_auth.h"
#include "ble_phone_drain.h"

#define APPLINK_OUT_BUF 512

struct AlConnIo
{
    int (*write)(void *ctx, const uint8_t *buf, size_t len);  // bytes accepted (0 = would block), -1 = connection dead
    void (*close)(void *ctx);                                 // close the TCP socket now (engine never touches it again)
    void (*onFrame)(void *ctx, uint8_t *frame, size_t len);   // binary message after login (the phone frame)
    void (*onAuthed)(void *ctx);                              // login ok ("ok" queued)
    void (*onClosed)(void *ctx, bool wasAuthed);              // connection finished, after io.close
    void *ctx;
};

struct AlConnInfo
{
    const char *call;
    const char *fw;
    const char *hw;
    const char *password;
};

enum AlConnState : uint8_t { ALC_IDLE = 0, ALC_HTTP, ALC_CHAL, ALC_AUTHED };

struct AlConnStats
{
    uint32_t frames_in, frames_out, auth_fail, busy;
    uint16_t last_close;
};

struct AlConn
{
    AlConnState state;
    AlConnIo io;
    AlConnStats st;
    uint8_t nonce[APPLINK_NONCE_LEN];
    uint32_t open_ms;  // alConnOpen time (login timeout)
    uint32_t last_rx;  // last byte received (silence timeout)
    uint32_t hb_ms;    // last node heartbeat queued
    uint16_t out_off;  // first unsent byte of out
    uint16_t out_len;  // end of queued bytes
    uint16_t http_len;
    bool announced;    // onAuthed was delivered: onClosed(wasAuthed) reports this, not the state
    bool pend_hb;
    bool pend_pong;
    uint8_t pong_len;
    uint8_t pong[125];
    uint8_t out[APPLINK_OUT_BUF];
    // HTTP phase precedes WebSocket phase: share the memory
    union
    {
        char http[APPLINK_HTTP_MAX];
        AlWsDecoder dec;
    } u;
};

void alConnInit(AlConn *c, const AlConnIo *io);
void alConnOpen(AlConn *c, const uint8_t nonce[APPLINK_NONCE_LEN], uint32_t now_ms);  // new TCP connection
void alConnRx(AlConn *c, const uint8_t *buf, size_t len, const AlConnInfo *info, AlLockout *lock, uint32_t now_ms);
void alConnPoll(AlConn *c, uint32_t now_ms);  // flush output, heartbeat, timeouts; call every loop pass
BlePhoneSend alConnSendFrame(AlConn *c, const uint8_t *frame, uint16_t len, uint32_t now_ms);
void alConnClose(AlConn *c, uint16_t code, const char *reason);  // reason -> {"t":"bye"} best effort, close frame, io.close, onClosed
bool alConnAuthed(const AlConn *c);
bool alConnActive(const AlConn *c);  // state != ALC_IDLE
