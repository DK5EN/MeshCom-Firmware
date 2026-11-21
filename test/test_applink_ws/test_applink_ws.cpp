// AppLink W1: SHA-1, base64, RFC 6455 handshake, frame encoder and incremental decoder.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "applink_ws.h"
#include "sha1_base64.h"

static void hex(const uint8_t *d, size_t n, char *o)
{
    for (size_t i = 0; i < n; i++)
        snprintf(o + 2 * i, 3, "%02x", d[i]);
}

static void expectSha(const char *msg, const char *want)
{
    uint8_t d[20];
    char h[41];
    sha1((const uint8_t *)msg, strlen(msg), d);
    hex(d, 20, h);
    TEST_ASSERT_EQUAL_STRING(want, h);
}

void test_sha1_vectors(void)
{
    expectSha("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    expectSha("", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    expectSha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

void test_sha1_block_boundaries(void)
{
    // 55, 56, 64 bytes of 'a' straddle the one/two padding block boundary
    uint8_t a[130];
    memset(a, 'a', sizeof(a));
    uint8_t d[20];
    char h[41];
    sha1(a, 55, d);
    hex(d, 20, h);
    TEST_ASSERT_EQUAL_STRING("c1c8bbdc22796e28c0e15163d20899b65621d65a", h);
    sha1(a, 56, d);
    hex(d, 20, h);
    TEST_ASSERT_EQUAL_STRING("c2db330f6083854c99d4b5bfb6e8f29f201be699", h);
    sha1(a, 64, d);
    hex(d, 20, h);
    TEST_ASSERT_EQUAL_STRING("0098ba824b5c16427bd7a1122a5a442a25ec644d", h);
}

void test_base64_rfc4648(void)
{
    const char *in[] = {"f", "fo", "foo", "foob", "fooba", "foobar"};
    const char *want[] = {"Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    char o[16];
    for (int i = 0; i < 6; i++)
    {
        size_t n = base64Encode((const uint8_t *)in[i], strlen(in[i]), o, sizeof(o));
        TEST_ASSERT_EQUAL_UINT32(strlen(want[i]), n);
        TEST_ASSERT_EQUAL_STRING(want[i], o);
    }
    TEST_ASSERT_EQUAL_UINT32(0, base64Encode((const uint8_t *)"", 0, o, sizeof(o)));
    TEST_ASSERT_EQUAL_STRING("", o);
}

void test_base64_out_too_small(void)
{
    char o[8];
    memset(o, 'x', sizeof(o));
    TEST_ASSERT_EQUAL_UINT32(0, base64Encode((const uint8_t *)"foob", 4, o, 8));  // needs 9
    TEST_ASSERT_EQUAL_CHAR('\0', o[0]);
    TEST_ASSERT_EQUAL_UINT32(4, base64Encode((const uint8_t *)"foo", 3, o, 5));  // exact fit
}

void test_ws_accept_key_rfc_example(void)
{
    char o[29];
    TEST_ASSERT_TRUE(wsAcceptKey("dGhlIHNhbXBsZSBub25jZQ==", o));
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", o);
    TEST_ASSERT_FALSE(wsAcceptKey("", o));
    TEST_ASSERT_FALSE(wsAcceptKey(0, o));
    char big[70];
    memset(big, 'k', sizeof(big));
    big[61] = '\0';
    TEST_ASSERT_FALSE(wsAcceptKey(big, o));
    big[60] = '\0';
    TEST_ASSERT_TRUE(wsAcceptKey(big, o));
}

// ------------------------------------------------------------ HTTP

static const char GOOD[] = "GET /applink HTTP/1.1\r\n"
                           "Host: 192.168.1.50:8002\r\n"
                           "Upgrade: websocket\r\n"
                           "Connection: Upgrade\r\n"
                           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                           "Sec-WebSocket-Version: 13\r\n"
                           "Sec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n";

static void expectAll(const char *req, AlHttpResult r, bool path, bool up, bool ver, bool proto)
{
    AlHttpReq q;
    TEST_ASSERT_EQUAL_INT(r, alHttpParse(req, strlen(req), &q));
    TEST_ASSERT_EQUAL(path, q.path_ok);
    TEST_ASSERT_EQUAL(up, q.upgrade_ok);
    TEST_ASSERT_EQUAL(ver, q.version_ok);
    TEST_ASSERT_EQUAL(proto, q.proto_ok);
}

void test_http_valid(void)
{
    AlHttpReq q;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, alHttpParse(GOOD, strlen(GOOD), &q));
    TEST_ASSERT_TRUE(q.path_ok && q.upgrade_ok && q.version_ok && q.proto_ok);
    TEST_ASSERT_EQUAL_STRING("dGhlIHNhbXBsZSBub25jZQ==", q.key);
}

void test_http_split_in_three_feeds(void)
{
    const size_t n = strlen(GOOD);
    AlHttpReq q;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_NEED_MORE, alHttpParse(GOOD, 10, &q));
    TEST_ASSERT_EQUAL_INT(AL_HTTP_NEED_MORE, alHttpParse(GOOD, n / 2, &q));
    TEST_ASSERT_EQUAL_INT(AL_HTTP_NEED_MORE, alHttpParse(GOOD, n - 1, &q));
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, alHttpParse(GOOD, n, &q));
}

void test_http_wrong_path_and_query(void)
{
    expectAll("GET /other HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n",
              AL_HTTP_OK, false, true, true, true);
    expectAll("GET /applink?x=1 HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n",
              AL_HTTP_OK, true, true, true, true);
    expectAll("GET /applinkx HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n",
              AL_HTTP_OK, false, true, false, false);
}

void test_http_missing_upgrade_wrong_version(void)
{
    expectAll("GET /applink HTTP/1.1\r\nHost: h\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n",
              AL_HTTP_OK, true, false, true, true);
    expectAll("GET /applink HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n",
              AL_HTTP_OK, true, false, true, true);
    expectAll("GET /applink HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 8\r\nSec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n",
              AL_HTTP_OK, true, true, false, true);
}

void test_http_subprotocol(void)
{
    expectAll("GET /applink HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\n\r\n",
              AL_HTTP_OK, true, true, true, false);
    expectAll("GET /applink HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: chat, meshcom-applink.1 , v2\r\n\r\n",
              AL_HTTP_OK, true, true, true, true);
    expectAll("GET /applink HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
              "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: meshcom-applink.2, meshcom-applink.10\r\n\r\n",
              AL_HTTP_OK, true, true, true, false);
}

void test_http_mixed_case_headers(void)
{
    expectAll("GET /applink HTTP/1.1\r\nHost: h\r\nuPGRADE: WebSocket\r\nCONNECTION: keep-alive, UPGRADE\r\n"
              "sec-websocket-key:   dGhlIHNhbXBsZSBub25jZQ==  \r\nSEC-WEBSOCKET-VERSION: 13\r\nsec-WebSocket-protocol:meshcom-applink.1\r\n\r\n",
              AL_HTTP_OK, true, true, true, true);
    AlHttpReq q;
    const char *r = "GET /applink HTTP/1.1\r\nHost: h\r\nsec-websocket-key:   dGhlIHNhbXBsZSBub25jZQ==  \r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, alHttpParse(r, strlen(r), &q));
    TEST_ASSERT_EQUAL_STRING("dGhlIHNhbXBsZSBub25jZQ==", q.key);
}

void test_http_bad(void)
{
    AlHttpReq q;
    const char *a = "POST /applink HTTP/1.1\r\nHost: h\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_BAD, alHttpParse(a, strlen(a), &q));
    const char *b = "GET /applink HTTP/2\r\nHost: h\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_BAD, alHttpParse(b, strlen(b), &q));
    const char *c = "GET /applink HTTP/1.1\r\nHost: h\r\nno colon here\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_BAD, alHttpParse(c, strlen(c), &q));
    const char *d = "GET /applink HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\n\r\n";  // no key
    TEST_ASSERT_EQUAL_INT(AL_HTTP_BAD, alHttpParse(d, strlen(d), &q));
    const char *e = "GET /applink\r\nHost: h\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_BAD, alHttpParse(e, strlen(e), &q));
}

void test_http_too_big(void)
{
    static char big[APPLINK_HTTP_MAX + 64];
    memset(big, 'a', sizeof(big));
    memcpy(big, "GET /applink HTTP/1.1\r\n", 23);
    AlHttpReq q;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_NEED_MORE, alHttpParse(big, APPLINK_HTTP_MAX - 1, &q));
    TEST_ASSERT_EQUAL_INT(AL_HTTP_TOO_BIG, alHttpParse(big, APPLINK_HTTP_MAX, &q));
    // terminator present but beyond the limit
    memcpy(big + APPLINK_HTTP_MAX - 2, "\r\n\r\n", 4);
    TEST_ASSERT_EQUAL_INT(AL_HTTP_TOO_BIG, alHttpParse(big, APPLINK_HTTP_MAX + 2, &q));
}

void test_http_accept_and_reject(void)
{
    AlHttpReq q;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, alHttpParse(GOOD, strlen(GOOD), &q));
    char o[300];
    size_t n = alHttpAccept(&q, o, sizeof(o));
    TEST_ASSERT_EQUAL_UINT32(strlen(o), n);
    TEST_ASSERT_EQUAL_INT(0, strncmp(o, "HTTP/1.1 101 Switching Protocols\r\n", 34));
    TEST_ASSERT_NOT_NULL(strstr(o, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Upgrade: websocket\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Connection: Upgrade\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Sec-WebSocket-Protocol: meshcom-applink.1\r\n"));
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", o + n - 4);
    TEST_ASSERT_EQUAL_UINT32(0, alHttpAccept(&q, o, n));  // one byte short (NUL)

    TEST_ASSERT_TRUE(alHttpReject(400, o, sizeof(o)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(o, "400 Bad Request"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Connection: close\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Content-Length: 0\r\n\r\n"));
    alHttpReject(403, o, sizeof(o));
    TEST_ASSERT_NOT_NULL(strstr(o, "403 Forbidden"));
    alHttpReject(503, o, sizeof(o));
    TEST_ASSERT_NOT_NULL(strstr(o, "503 Service Unavailable"));
    TEST_ASSERT_EQUAL_UINT32(0, alHttpReject(400, o, 10));
}

#define KEY_OK "dGhlIHNhbXBsZSBub25jZQ=="
#define REQ_HEAD "GET /applink HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
#define REQ_TAIL "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n"

static AlHttpResult parseStr(const char *r, AlHttpReq *q)
{
    return alHttpParse(r, strlen(r), q);
}

void test_http_missing_host_is_bad(void)
{
    AlHttpReq q;
    const char *noHost = "GET /applink HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         "Sec-WebSocket-Key: " KEY_OK "\r\n" REQ_TAIL;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_BAD, parseStr(noHost, &q));
    TEST_ASSERT_FALSE(q.host_ok);
    const char *emptyHost = "GET /applink HTTP/1.1\r\nHost:  \r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: " KEY_OK "\r\n" REQ_TAIL;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_BAD, parseStr(emptyHost, &q));
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, alHttpParse(GOOD, strlen(GOOD), &q));
    TEST_ASSERT_TRUE(q.host_ok);
    // header name matching is case-insensitive
    const char *lcHost = "GET /applink HTTP/1.1\r\nhOsT: a:1\r\nSec-WebSocket-Key: " KEY_OK "\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, parseStr(lcHost, &q));
    TEST_ASSERT_TRUE(q.host_ok);
}

static void expectKey(const char *key, AlHttpResult want)
{
    char r[400];
    snprintf(r, sizeof(r), REQ_HEAD "Sec-WebSocket-Key: %s\r\n" REQ_TAIL, key);
    AlHttpReq q;
    TEST_ASSERT_EQUAL_INT_MESSAGE(want, parseStr(r, &q), key);
}

void test_http_key_must_be_base64_of_16_bytes(void)
{
    expectKey(KEY_OK, AL_HTTP_OK);
    expectKey("AAAAAAAAAAAAAAAAAAAAAA==", AL_HTTP_OK);
    expectKey("abc", AL_HTTP_BAD);
    expectKey("dGhlIHNhbXBsZSBub25jZQ=", AL_HTTP_BAD);      // 23 chars
    expectKey("dGhlIHNhbXBsZSBub25jZQ===", AL_HTTP_BAD);    // 25 chars
    expectKey("dGhlIHNhbXBsZSBub25jZQAA", AL_HTTP_BAD);     // 24 chars, no "=="
    expectKey("dGhlIHNhbXBsZSBub25jZQ=A", AL_HTTP_BAD);     // second pad missing
    expectKey("dGhlIHNhbXBsZSBub25jZ*==", AL_HTTP_BAD);     // outside the alphabet
    expectKey("dGhlIHNhbXBs ZSBub25jZQ==", AL_HTTP_BAD);    // blank inside (25 chars)
    expectKey("dGhlIHNhbXBs-SBub25jZQ==", AL_HTTP_BAD);     // base64url char, 24 chars
    expectKey("dGhlIHN=bXBsZSBub25jZQ==", AL_HTTP_BAD);     // '=' in the middle
    expectKey("dGhlIHNhbXBsZSBub25j+/==", AL_HTTP_OK);      // '+' and '/' belong to the alphabet
}

void test_http_reject_426_carries_version(void)
{
    char o[200];
    size_t n = alHttpReject(426, o, sizeof(o));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT32(strlen(o), n);
    TEST_ASSERT_NOT_NULL(strstr(o, "HTTP/1.1 426 Upgrade Required\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Sec-WebSocket-Version: 13\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Connection: close\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(o, "Content-Length: 0\r\n\r\n"));
    TEST_ASSERT_EQUAL_STRING("\r\n\r\n", o + n - 4);
    TEST_ASSERT_EQUAL_UINT32(0, alHttpReject(426, o, 40));  // too small
    // the other statuses stay as before and do not advertise a version
    static const int codes[] = {400, 403, 503, 999};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    {
        TEST_ASSERT_TRUE(alHttpReject(codes[i], o, sizeof(o)) > 0);
        TEST_ASSERT_NULL(strstr(o, "Sec-WebSocket-Version"));
    }
    alHttpReject(999, o, sizeof(o));
    TEST_ASSERT_NOT_NULL(strstr(o, "400 Bad Request"));
}

void test_http_status_for(void)
{
    AlHttpReq q;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, alHttpParse(GOOD, strlen(GOOD), &q));
    TEST_ASSERT_EQUAL_INT(0, alHttpStatusFor(&q));
    TEST_ASSERT_EQUAL_INT(400, alHttpStatusFor(0));

    // wrong version only -> 426
    const char *v8 = REQ_HEAD "Sec-WebSocket-Key: " KEY_OK "\r\nSec-WebSocket-Version: 8\r\n"
                              "Sec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, parseStr(v8, &q));
    TEST_ASSERT_FALSE(q.version_ok);
    TEST_ASSERT_EQUAL_INT(426, alHttpStatusFor(&q));
    // missing version -> 426 as well
    const char *nov = REQ_HEAD "Sec-WebSocket-Key: " KEY_OK "\r\nSec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, parseStr(nov, &q));
    TEST_ASSERT_EQUAL_INT(426, alHttpStatusFor(&q));

    // version plus anything else wrong -> 400
    const char *v8path = "GET /x HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                         "Sec-WebSocket-Key: " KEY_OK "\r\nSec-WebSocket-Version: 8\r\n"
                         "Sec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, parseStr(v8path, &q));
    TEST_ASSERT_EQUAL_INT(400, alHttpStatusFor(&q));
    const char *noproto = REQ_HEAD "Sec-WebSocket-Key: " KEY_OK "\r\nSec-WebSocket-Version: 13\r\n\r\n";
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, parseStr(noproto, &q));
    TEST_ASSERT_EQUAL_INT(400, alHttpStatusFor(&q));
    const char *noupg = "GET /applink HTTP/1.1\r\nHost: h\r\nSec-WebSocket-Key: " KEY_OK "\r\n" REQ_TAIL;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, parseStr(noupg, &q));
    TEST_ASSERT_EQUAL_INT(400, alHttpStatusFor(&q));
    const char *wrongpath = "GET /x HTTP/1.1\r\nHost: h\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: " KEY_OK "\r\n" REQ_TAIL;
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, parseStr(wrongpath, &q));
    TEST_ASSERT_EQUAL_INT(400, alHttpStatusFor(&q));

    // every flag individually
    TEST_ASSERT_EQUAL_INT(AL_HTTP_OK, alHttpParse(GOOD, strlen(GOOD), &q));
    q.host_ok = false;
    TEST_ASSERT_EQUAL_INT(400, alHttpStatusFor(&q));
    q.host_ok = true;
    q.version_ok = false;
    TEST_ASSERT_EQUAL_INT(426, alHttpStatusFor(&q));
    q.upgrade_ok = false;
    TEST_ASSERT_EQUAL_INT(400, alHttpStatusFor(&q));
}

// ------------------------------------------------------------ encoder

static void encodeLen(size_t len, size_t hdr)
{
    static uint8_t pl[300], out[320];
    for (size_t i = 0; i < len; i++)
        pl[i] = (uint8_t)i;
    size_t n = alWsEncode(AL_OP_BIN, pl, len, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(hdr + len, n);
    TEST_ASSERT_EQUAL_HEX8(0x82, out[0]);
    if (hdr == 2)
        TEST_ASSERT_EQUAL_UINT8(len, out[1]);
    else
    {
        TEST_ASSERT_EQUAL_UINT8(126, out[1]);
        TEST_ASSERT_EQUAL_UINT8(len >> 8, out[2]);
        TEST_ASSERT_EQUAL_UINT8(len & 0xFF, out[3]);
    }
    if (len)
        TEST_ASSERT_EQUAL_MEMORY(pl, out + hdr, len);
    if (n > 0)
        TEST_ASSERT_EQUAL_UINT32(0, alWsEncode(AL_OP_BIN, pl, len, out, n - 1));
}

void test_encode_lengths(void)
{
    encodeLen(0, 2);
    encodeLen(125, 2);
    encodeLen(126, 4);
    encodeLen(257, 4);
}

void test_encode_refusals_and_close(void)
{
    uint8_t out[200], pl[70000 / 1000];
    TEST_ASSERT_EQUAL_UINT32(0, alWsEncode(AL_OP_BIN, pl, 65536, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, alWsEncode(AL_OP_PING, pl, 126, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT32(0, alWsEncode(AL_OP_BIN, pl, 0, out, 1));
    size_t n = alWsEncodeClose(1000, "bye", out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32(7, n);
    TEST_ASSERT_EQUAL_HEX8(0x88, out[0]);
    TEST_ASSERT_EQUAL_UINT8(5, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x03, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0xE8, out[3]);
    TEST_ASSERT_EQUAL_MEMORY("bye", out + 4, 3);
    TEST_ASSERT_EQUAL_UINT32(4, alWsEncodeClose(4401, 0, out, sizeof(out)));
    char longr[200];
    memset(longr, 'r', sizeof(longr) - 1);
    longr[199] = '\0';
    TEST_ASSERT_EQUAL_UINT32(2 + 125, alWsEncodeClose(1000, longr, out, sizeof(out)));
}

// ------------------------------------------------------------ decoder

static size_t mkFrame(uint8_t b0, const uint8_t *pl, size_t len, uint8_t *out, bool masked = true)
{
    static const uint8_t m[4] = {0x37, 0xFA, 0x21, 0x3D};
    size_t o = 0;
    out[o++] = b0;
    const uint8_t mb = masked ? 0x80 : 0;
    if (len < 126)
        out[o++] = (uint8_t)(mb | len);
    else
    {
        out[o++] = (uint8_t)(mb | 126);
        out[o++] = (uint8_t)(len >> 8);
        out[o++] = (uint8_t)len;
    }
    if (masked)
    {
        memcpy(out + o, m, 4);
        o += 4;
    }
    for (size_t i = 0; i < len; i++)
        out[o + i] = masked ? (uint8_t)(pl[i] ^ m[i & 3]) : pl[i];
    return o + len;
}

void test_decode_binary_3(void)
{
    AlWsDecoder d;
    alWsDecoderInit(&d);
    const uint8_t pl[3] = {1, 2, 3};
    uint8_t f[16];
    size_t n = mkFrame(0x82, pl, 3, f);
    AlWsEvent ev;
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_BINARY, ev);
    TEST_ASSERT_EQUAL_UINT32(3, d.msg_len);
    TEST_ASSERT_EQUAL_MEMORY(pl, d.msg, 3);
}

void test_decode_300_and_301(void)
{
    static uint8_t pl[301], f[320];
    for (int i = 0; i < 301; i++)
        pl[i] = (uint8_t)(i * 7);
    AlWsDecoder d;
    alWsDecoderInit(&d);
    size_t n = mkFrame(0x82, pl, 300, f);
    AlWsEvent ev;
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_BINARY, ev);
    TEST_ASSERT_EQUAL_UINT32(300, d.msg_len);
    TEST_ASSERT_EQUAL_MEMORY(pl, d.msg, 300);

    alWsDecoderInit(&d);
    n = mkFrame(0x82, pl, 301, f);
    alWsFeed(&d, f, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_ERROR, ev);
    TEST_ASSERT_EQUAL_UINT16(1009, d.error_code);
    // sticky
    TEST_ASSERT_EQUAL_UINT32(0, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_ERROR, ev);
}

void test_decode_64bit_length_is_1009(void)
{
    AlWsDecoder d;
    alWsDecoderInit(&d);
    const uint8_t f[] = {0x82, 0xFF, 0, 0, 0, 1, 0, 0, 0, 0, 1, 2, 3, 4};
    AlWsEvent ev;
    alWsFeed(&d, f, sizeof(f), &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_ERROR, ev);
    TEST_ASSERT_EQUAL_UINT16(1009, d.error_code);
}

static void expectProto(const uint8_t *f, size_t n)
{
    AlWsDecoder d;
    alWsDecoderInit(&d);
    AlWsEvent ev;
    alWsFeed(&d, f, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_ERROR, ev);
    TEST_ASSERT_EQUAL_UINT16(1002, d.error_code);
}

void test_decode_protocol_errors(void)
{
    uint8_t f[160];
    const uint8_t pl[3] = {1, 2, 3};
    size_t n = mkFrame(0x82, pl, 3, f, false);  // unmasked
    expectProto(f, n);
    n = mkFrame(0xC2, pl, 3, f);  // RSV1
    expectProto(f, n);
    n = mkFrame(0x83, pl, 3, f);  // unknown opcode
    expectProto(f, n);
    n = mkFrame(0x8B, pl, 3, f);  // unknown control opcode
    expectProto(f, n);
    uint8_t big[130] = {0};
    n = mkFrame(0x89, big, 126, f);  // ping > 125
    expectProto(f, n);
    n = mkFrame(0x09, pl, 3, f);  // fragmented ping
    expectProto(f, n);
    n = mkFrame(0x80, pl, 3, f);  // continuation without start
    expectProto(f, n);
    n = mkFrame(0x88, pl, 1, f);  // close with a 1-byte payload
    expectProto(f, n);

    // new data frame while fragmenting
    AlWsDecoder d;
    alWsDecoderInit(&d);
    AlWsEvent ev;
    n = mkFrame(0x01, pl, 3, f);
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_NONE, ev);
    n = mkFrame(0x82, pl, 3, f);
    alWsFeed(&d, f, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_ERROR, ev);
    TEST_ASSERT_EQUAL_UINT16(1002, d.error_code);
}

void test_decode_fragmented_text_with_ping(void)
{
    AlWsDecoder d;
    alWsDecoderInit(&d);
    uint8_t buf[100];
    size_t n = 0;
    n += mkFrame(0x01, (const uint8_t *)"Hel", 3, buf + n);   // TEXT, no FIN
    n += mkFrame(0x89, (const uint8_t *)"pp", 2, buf + n);    // PING
    n += mkFrame(0x00, (const uint8_t *)"lo ", 3, buf + n);   // CONT
    n += mkFrame(0x80, (const uint8_t *)"you", 3, buf + n);   // CONT + FIN
    size_t pos = 0;
    AlWsEvent ev;
    pos += alWsFeed(&d, buf + pos, n - pos, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_PING, ev);
    TEST_ASSERT_EQUAL_UINT32(2, d.ctl_len);
    TEST_ASSERT_EQUAL_MEMORY("pp", d.ctl, 2);
    pos += alWsFeed(&d, buf + pos, n - pos, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_TEXT, ev);
    TEST_ASSERT_EQUAL_UINT32(n, pos);
    TEST_ASSERT_EQUAL_UINT32(9, d.msg_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello you", d.msg, 9);
    // the next message starts clean
    const uint8_t pl[1] = {9};
    n = mkFrame(0x82, pl, 1, buf);
    alWsFeed(&d, buf, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_BINARY, ev);
    TEST_ASSERT_EQUAL_UINT32(1, d.msg_len);
}

void test_decode_close(void)
{
    AlWsDecoder d;
    alWsDecoderInit(&d);
    uint8_t f[32];
    AlWsEvent ev;
    const uint8_t withCode[] = {0x03, 0xE8, 'o', 'k'};
    size_t n = mkFrame(0x88, withCode, 4, f);
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_CLOSE, ev);
    TEST_ASSERT_EQUAL_UINT16(1000, d.close_code);
    TEST_ASSERT_EQUAL_UINT32(4, d.ctl_len);

    alWsDecoderInit(&d);
    n = mkFrame(0x88, 0, 0, f);
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_CLOSE, ev);
    TEST_ASSERT_EQUAL_UINT16(1005, d.close_code);
    TEST_ASSERT_EQUAL_UINT32(0, d.ctl_len);
}

void test_decode_byte_by_byte(void)
{
    static uint8_t pl[200], f[220];
    for (int i = 0; i < 200; i++)
        pl[i] = (uint8_t)(255 - i);
    size_t n = mkFrame(0x82, pl, 200, f);
    AlWsDecoder d;
    alWsDecoderInit(&d);
    AlWsEvent ev = AL_WS_NONE;
    for (size_t i = 0; i < n; i++)
    {
        TEST_ASSERT_EQUAL_INT(AL_WS_NONE, ev);
        TEST_ASSERT_EQUAL_UINT32(1, alWsFeed(&d, f + i, 1, &ev));
    }
    TEST_ASSERT_EQUAL_INT(AL_WS_BINARY, ev);
    TEST_ASSERT_EQUAL_UINT32(200, d.msg_len);
    TEST_ASSERT_EQUAL_MEMORY(pl, d.msg, 200);
}

void test_decode_two_frames_in_one_buffer(void)
{
    uint8_t buf[64];
    const uint8_t a[2] = {1, 2}, b[3] = {7, 8, 9};
    size_t n1 = mkFrame(0x82, a, 2, buf);
    size_t n2 = mkFrame(0x81, b, 3, buf + n1);
    AlWsDecoder d;
    alWsDecoderInit(&d);
    AlWsEvent ev;
    size_t c = alWsFeed(&d, buf, n1 + n2, &ev);
    TEST_ASSERT_EQUAL_UINT32(n1, c);
    TEST_ASSERT_EQUAL_INT(AL_WS_BINARY, ev);
    TEST_ASSERT_EQUAL_UINT32(2, d.msg_len);
    c = alWsFeed(&d, buf + n1, n2, &ev);
    TEST_ASSERT_EQUAL_UINT32(n2, c);
    TEST_ASSERT_EQUAL_INT(AL_WS_TEXT, ev);
    TEST_ASSERT_EQUAL_UINT32(3, d.msg_len);
    TEST_ASSERT_EQUAL_MEMORY(b, d.msg, 3);
    TEST_ASSERT_EQUAL_UINT32(0, alWsFeed(&d, buf, 0, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_NONE, ev);
}

void test_decode_empty_binary_frame(void)
{
    AlWsDecoder d;
    alWsDecoderInit(&d);
    uint8_t f[8];
    size_t n = mkFrame(0x82, 0, 0, f);
    AlWsEvent ev;
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_BINARY, ev);
    TEST_ASSERT_EQUAL_UINT32(0, d.msg_len);
}

void test_decode_fragment_total_limit_1009(void)
{
    static uint8_t pl[200], f[420];
    for (int i = 0; i < 200; i++)
        pl[i] = (uint8_t)(i ^ 0x5A);
    AlWsDecoder d;
    AlWsEvent ev;
    // 200 + 200 > APPLINK_MAX_IN: the second fragment header is refused
    alWsDecoderInit(&d);
    size_t n = mkFrame(0x01, pl, 200, f);  // TEXT, no FIN
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_NONE, ev);
    n = mkFrame(0x80, pl, 200, f);  // CONT + FIN
    alWsFeed(&d, f, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_ERROR, ev);
    TEST_ASSERT_EQUAL_UINT16(1009, d.error_code);
    // 200 + 101 is one over, 200 + 100 fits exactly
    alWsDecoderInit(&d);
    n = mkFrame(0x01, pl, 200, f);
    alWsFeed(&d, f, n, &ev);
    n = mkFrame(0x80, pl, 101, f);
    alWsFeed(&d, f, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_ERROR, ev);
    TEST_ASSERT_EQUAL_UINT16(1009, d.error_code);
    alWsDecoderInit(&d);
    n = mkFrame(0x01, pl, 200, f);
    alWsFeed(&d, f, n, &ev);
    n = mkFrame(0x80, pl, 100, f);
    alWsFeed(&d, f, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_TEXT, ev);
    TEST_ASSERT_EQUAL_UINT32(300, d.msg_len);

    // 150 + 150 = 300 is accepted and reassembled in order
    alWsDecoderInit(&d);
    n = mkFrame(0x01, pl, 150, f);
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_NONE, ev);
    n = mkFrame(0x80, pl + 50, 150, f);
    TEST_ASSERT_EQUAL_UINT32(n, alWsFeed(&d, f, n, &ev));
    TEST_ASSERT_EQUAL_INT(AL_WS_TEXT, ev);
    TEST_ASSERT_EQUAL_UINT32(300, d.msg_len);
    TEST_ASSERT_EQUAL_MEMORY(pl, d.msg, 150);
    TEST_ASSERT_EQUAL_MEMORY(pl + 50, d.msg + 150, 150);
}

static void expectCloseCode(uint16_t code, bool allowed)
{
    AlWsDecoder d;
    alWsDecoderInit(&d);
    uint8_t pl[2] = {(uint8_t)(code >> 8), (uint8_t)code};
    uint8_t f[16];
    AlWsEvent ev = AL_WS_NONE;
    size_t n = mkFrame(0x88, pl, 2, f);
    alWsFeed(&d, f, n, &ev);
    char msg[24];
    snprintf(msg, sizeof(msg), "close code %u", (unsigned)code);
    if (allowed)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(AL_WS_CLOSE, ev, msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(code, d.close_code, msg);
    }
    else
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(AL_WS_ERROR, ev, msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(1002, d.error_code, msg);
    }
}

void test_decode_close_code_validity(void)
{
    static const uint16_t good[] = {1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 3000, 3999, 4000, 4999};
    static const uint16_t bad[] = {0, 1, 999, 1004, 1005, 1006, 1015, 1016, 2000, 2999, 5000, 5001, 65535};
    for (size_t i = 0; i < sizeof(good) / sizeof(good[0]); i++)
        expectCloseCode(good[i], true);
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        expectCloseCode(bad[i], false);
    // a code-less CLOSE stays valid (reported as 1005 internally, never sent)
    AlWsDecoder d;
    alWsDecoderInit(&d);
    uint8_t f[8];
    AlWsEvent ev;
    size_t n = mkFrame(0x88, 0, 0, f);
    alWsFeed(&d, f, n, &ev);
    TEST_ASSERT_EQUAL_INT(AL_WS_CLOSE, ev);
    TEST_ASSERT_EQUAL_UINT16(1005, d.close_code);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha1_vectors);
    RUN_TEST(test_sha1_block_boundaries);
    RUN_TEST(test_base64_rfc4648);
    RUN_TEST(test_base64_out_too_small);
    RUN_TEST(test_ws_accept_key_rfc_example);
    RUN_TEST(test_http_valid);
    RUN_TEST(test_http_split_in_three_feeds);
    RUN_TEST(test_http_wrong_path_and_query);
    RUN_TEST(test_http_missing_upgrade_wrong_version);
    RUN_TEST(test_http_subprotocol);
    RUN_TEST(test_http_mixed_case_headers);
    RUN_TEST(test_http_bad);
    RUN_TEST(test_http_too_big);
    RUN_TEST(test_http_accept_and_reject);
    RUN_TEST(test_http_missing_host_is_bad);
    RUN_TEST(test_http_key_must_be_base64_of_16_bytes);
    RUN_TEST(test_http_reject_426_carries_version);
    RUN_TEST(test_http_status_for);
    RUN_TEST(test_encode_lengths);
    RUN_TEST(test_encode_refusals_and_close);
    RUN_TEST(test_decode_binary_3);
    RUN_TEST(test_decode_300_and_301);
    RUN_TEST(test_decode_64bit_length_is_1009);
    RUN_TEST(test_decode_protocol_errors);
    RUN_TEST(test_decode_fragmented_text_with_ping);
    RUN_TEST(test_decode_close);
    RUN_TEST(test_decode_byte_by_byte);
    RUN_TEST(test_decode_two_frames_in_one_buffer);
    RUN_TEST(test_decode_empty_binary_frame);
    RUN_TEST(test_decode_fragment_total_limit_1009);
    RUN_TEST(test_decode_close_code_validity);
    return UNITY_END();
}

void setUp(void) {}
void tearDown(void) {}
