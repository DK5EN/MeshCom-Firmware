// AppLink (docs/applink-api-contract.md sections 2, 4, 7) -- transport-independent WebSocket codec:
// HTTP upgrade parser/answer, server frame encoder, incremental client frame decoder.
// Arduino-free, no allocation; the caller owns the socket.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define APPLINK_PORT 8002
#define APPLINK_PATH "/applink"
#define APPLINK_SUBPROTO "meshcom-applink.1"
#define APPLINK_MAX_IN 300     // largest client message (binary or text), contract 4.1
#define APPLINK_MAX_OUT 257    // largest node binary message, contract 4.2
#define APPLINK_HTTP_MAX 1024  // largest upgrade request incl. headers

enum AlHttpResult : uint8_t { AL_HTTP_NEED_MORE = 0, AL_HTTP_OK, AL_HTTP_BAD, AL_HTTP_TOO_BIG };

struct AlHttpReq
{
    char key[64];
    bool path_ok;
    bool upgrade_ok;
    bool version_ok;
    bool proto_ok;
    bool host_ok;  // a non-empty Host header was present (RFC 6455 4.2.1); alHttpParse returns BAD without it
};

// buf holds the bytes received so far (need not be NUL terminated). NEED_MORE until "\r\n\r\n" is
// seen; TOO_BIG when len >= APPLINK_HTTP_MAX without the end. OK = syntactically valid GET request
// line plus headers with a Host and a Sec-WebSocket-Key; the caller checks the flags (alHttpStatusFor
// maps them to the status to answer). A missing Host, or a key that is not the base64 of 16 bytes
// (exactly 24 chars, base64 alphabet, ending "=="), is BAD.
AlHttpResult alHttpParse(const char *buf, size_t len, AlHttpReq *out);
// 101 answer with Upgrade, Connection, Sec-WebSocket-Accept, Sec-WebSocket-Protocol. Length, 0 if too small.
size_t alHttpAccept(const AlHttpReq *req, char *out, size_t out_size);
// status 400, 403, 426 or 503 (anything else becomes 400); Connection: close, Content-Length: 0.
// 426 also carries "Sec-WebSocket-Version: 13" (RFC 6455 4.4).
size_t alHttpReject(int status, char *out, size_t out_size);
// Status to answer for a request alHttpParse accepted as OK: 0 when path, upgrade, version, proto and
// host are all ok, 426 when only the version is wrong, 400 for anything else (also r == NULL).
int alHttpStatusFor(const AlHttpReq *r);

// server -> client frames: unmasked, FIN set, never fragmented
enum AlWsOp : uint8_t { AL_OP_CONT = 0x0, AL_OP_TEXT = 0x1, AL_OP_BIN = 0x2, AL_OP_CLOSE = 0x8, AL_OP_PING = 0x9, AL_OP_PONG = 0xA };

// Total bytes, 0 if out is too small, len > 65535 or a control payload > 125.
size_t alWsEncode(uint8_t opcode, const uint8_t *payload, size_t len, uint8_t *out, size_t out_size);
// reason may be NULL; truncated to 123 bytes.
size_t alWsEncodeClose(uint16_t code, const char *reason, uint8_t *out, size_t out_size);

// client -> server incremental decoder
enum AlWsEvent : uint8_t { AL_WS_NONE = 0, AL_WS_TEXT, AL_WS_BINARY, AL_WS_PING, AL_WS_PONG, AL_WS_CLOSE, AL_WS_ERROR };

struct AlWsDecoder
{
    // internal state
    uint8_t phase;
    uint8_t opcode;
    bool fin;
    uint8_t frag_op;   // opcode of the message being reassembled, 0 = none
    uint8_t done;      // 0 none, 1 data message pending release, 2 control pending release
    uint8_t ext_need;  // extended length bytes still to read
    uint8_t mask_got;
    uint8_t mask[4];
    uint64_t ext_len;
    uint64_t remaining;
    uint64_t mask_pos;
    // outputs
    uint8_t msg[APPLINK_MAX_IN];
    size_t msg_len;
    uint8_t ctl[125];
    size_t ctl_len;
    uint16_t close_code;  // 1005 if the CLOSE carried no code
    uint16_t error_code;  // 1002 protocol, 1009 too big
};

void alWsDecoderInit(AlWsDecoder *d);
// Consumes bytes and stops right after one complete message or control frame, reported in *ev; the
// caller handles it and calls again with the remaining bytes. Returns bytes consumed. TEXT/BINARY
// payload in msg/msg_len (reassembled; control frames may interleave fragments), PING/PONG payload
// in ctl/ctl_len, CLOSE: close_code and the full payload in ctl. ERROR: error_code 1002 or 1009;
// afterwards the decoder stays in error (returns 0, AL_WS_ERROR). A CLOSE whose code may not appear
// on the wire (< 1000, 1004-1006, 1015-2999, >= 5000) is a 1002 error. Text is not UTF-8 validated.
size_t alWsFeed(AlWsDecoder *d, const uint8_t *in, size_t len, AlWsEvent *ev);
