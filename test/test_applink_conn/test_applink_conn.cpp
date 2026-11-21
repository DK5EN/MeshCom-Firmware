// AppLink connection engine: full life of one connection against a fake transport.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include "applink_conn.h"

// ---------------------------------------------------------------- fake io
struct Fake
{
    uint8_t wr[4096];
    size_t wr_len;
    long budget;  // bytes the next writes accept in total, <0 unlimited
    bool dead;    // write returns -1
    bool dead_on_ok;       // write returns -1 for exactly the chunk holding the "ok" message
    uint16_t close_in_frame;  // onFrame calls alConnClose(this code) (takeover case), 0 = off
    bool io_closed;
    int closes, closed_calls, authed_calls, frames;
    bool closed_was_authed;
    int violations;  // io callback after close
    uint8_t last_frame[400];
    size_t last_frame_len;
};
static Fake F;
static AlConn C;
static AlLockout L;
static AlConnInfo INFO = {"DK5EN-1", "4.35a.11.21", "HELTEC_V3", "secret"};
static const uint8_t NONCE[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

static int fwrite_(void *, const uint8_t *b, size_t n)
{
    if (F.io_closed)
        F.violations++;
    if (F.dead)
        return -1;
    if (F.dead_on_ok)
    {
        const char *needle = "\"t\":\"ok\"";
        const size_t nl = strlen(needle);
        for (size_t i = 0; i + nl <= n; i++)
            if (memcmp(b + i, needle, nl) == 0)
            {
                F.dead = true;
                return -1;
            }
    }
    size_t k = n;
    if (F.budget >= 0 && (size_t)F.budget < k)
        k = (size_t)F.budget;
    if (F.budget >= 0)
        F.budget -= (long)k;
    if (F.wr_len + k > sizeof(F.wr))
        k = sizeof(F.wr) - F.wr_len;
    memcpy(F.wr + F.wr_len, b, k);
    F.wr_len += k;
    return (int)k;
}
static void fclose_(void *)
{
    if (F.io_closed)
        F.violations++;
    F.io_closed = true;
    F.closes++;
}
static void fframe(void *, uint8_t *f, size_t n)
{
    if (F.io_closed)
        F.violations++;
    F.frames++;
    F.last_frame_len = n;
    memcpy(F.last_frame, f, n);
    if (F.close_in_frame)
        alConnClose(&C, F.close_in_frame, "taken over");
}
static void fauthed(void *)
{
    if (F.io_closed)
        F.violations++;
    F.authed_calls++;
}
static void fclosed(void *, bool was)
{
    if (!F.io_closed)
        F.violations++;
    F.closed_calls++;
    F.closed_was_authed = was;
}

static void openConn(uint32_t now)
{
    memset(&F, 0, sizeof(F));
    F.budget = -1;
    AlConnIo io = {fwrite_, fclose_, fframe, fauthed, fclosed, NULL};
    alConnInit(&C, &io);
    alConnOpen(&C, NONCE, now);
}
static void reopen(uint32_t now)  // new connection, same lockout
{
    memset(&F, 0, sizeof(F));
    F.budget = -1;
    alConnOpen(&C, NONCE, now);
}
void setUp(void)
{
    alLockoutInit(&L);
}
void tearDown(void) {}

// ---------------------------------------------------------------- client helpers
static const char REQ[] = "GET /applink HTTP/1.1\r\nHost: 192.168.1.50:8002\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
                          "Sec-WebSocket-Protocol: meshcom-applink.1\r\n\r\n";

static size_t cframe(uint8_t op, const uint8_t *p, size_t n, uint8_t *out)
{
    const uint8_t mk[4] = {0x37, 0xfa, 0x21, 0x3d};
    size_t i = 0;
    out[i++] = 0x80 | op;
    if (n < 126)
        out[i++] = 0x80 | (uint8_t)n;
    else
    {
        out[i++] = 0x80 | 126;
        out[i++] = (uint8_t)(n >> 8);
        out[i++] = (uint8_t)n;
    }
    memcpy(out + i, mk, 4);
    i += 4;
    for (size_t k = 0; k < n; k++)
        out[i + k] = p[k] ^ mk[k & 3];
    return i + n;
}
static size_t ctext(const char *s, uint8_t *out) { return cframe(AL_OP_TEXT, (const uint8_t *)s, strlen(s), out); }

static void rx(const uint8_t *b, size_t n, uint32_t now) { alConnRx(&C, b, n, &INFO, &L, now); }
static void rxText(const char *s, uint32_t now)
{
    uint8_t b[400];
    rx(b, ctext(s, b), now);
}
static void rxHttp(uint32_t now) { rx((const uint8_t *)REQ, strlen(REQ), now); }

// parse server frames out of F.wr starting after the HTTP answer
struct Srv
{
    size_t pos;
    uint8_t op;
    const uint8_t *p;
    size_t n;
};
static void srvStart(Srv *s, bool http)
{
    s->pos = 0;
    if (http)
    {
        const char *e = strstr((const char *)F.wr, "\r\n\r\n");
        s->pos = e ? (size_t)((const uint8_t *)e - F.wr) + 4 : 0;
    }
}
static bool srvNext(Srv *s)
{
    if (s->pos + 2 > F.wr_len)
        return false;
    const uint8_t *b = F.wr + s->pos;
    s->op = b[0] & 0x0f;
    size_t n = b[1] & 0x7f, h = 2;
    if (n == 126)
    {
        n = (size_t)(b[2] << 8 | b[3]);
        h = 4;
    }
    if (s->pos + h + n > F.wr_len)
        return false;
    s->p = b + h;
    s->n = n;
    s->pos += h + n;
    return true;
}
static bool srvTextHas(Srv *s, const char *needle)
{
    return s->op == AL_OP_TEXT && strstr(std::string((const char *)s->p, s->n).c_str(), needle) != NULL;
}

static void sendAuth(const char *pw, uint32_t now)
{
    uint8_t m[32];
    char hex[65], j[100];
    alAuthExpectedMac(pw, NONCE, m);
    hexLower(m, 32, hex);
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s\"}", hex);
    rxText(j, now);
}
static void login(uint32_t now)
{
    rxHttp(now);
    sendAuth("secret", now);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
    F.wr_len = 0;
}

// ---------------------------------------------------------------- tests
static void test_happy_path(void)
{
    openConn(1000);
    rxHttp(1000);
    TEST_ASSERT_EQUAL(ALC_CHAL, C.state);
    TEST_ASSERT_NOT_NULL(strstr((const char *)F.wr, "101"));
    Srv s;
    srvStart(&s, true);
    TEST_ASSERT_TRUE(srvNext(&s));
    char want[300];
    TEST_ASSERT_TRUE(srvTextHas(&s, "\"t\":\"chal\""));
    TEST_ASSERT_TRUE(srvTextHas(&s, "\"nonce\":\"0102030405060708090a0b0c0d0e0f10\""));
    TEST_ASSERT_TRUE(srvTextHas(&s, "\"call\":\"DK5EN-1\""));
    (void)want;
    sendAuth("secret", 1010);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
    TEST_ASSERT_EQUAL(1, F.authed_calls);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_TRUE(srvTextHas(&s, "{\"t\":\"ok\",\"mtu\":257}"));
    uint8_t ph[4] = {0x04, 0x10, 0x20, 0x30}, b[64];
    rx(b, cframe(AL_OP_BIN, ph, 4, b), 1020);
    TEST_ASSERT_EQUAL(1, F.frames);
    TEST_ASSERT_EQUAL(4, F.last_frame_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ph, F.last_frame, 4);
    TEST_ASSERT_EQUAL(1, C.st.frames_in);
}

static void test_http_one_byte_chunks(void)
{
    openConn(0);
    for (size_t i = 0; i < strlen(REQ); i++)
        rx((const uint8_t *)REQ + i, 1, 0);
    TEST_ASSERT_EQUAL(ALC_CHAL, C.state);
    sendAuth("secret", 5);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
}

static void test_http_plus_ws_frame(void)
{
    openConn(0);
    uint8_t b[1100];
    size_t n = strlen(REQ);
    memcpy(b, REQ, n);
    uint8_t m[32];
    char hex[65], j[100];
    alAuthExpectedMac("secret", NONCE, m);
    hexLower(m, 32, hex);
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s\"}", hex);
    n += ctext(j, b + n);
    rx(b, n, 0);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
}

static void test_bad_request(void)
{
    openConn(0);
    const char *bad = "POST / HTTP/1.1\r\nHost: x\r\n\r\n";
    rx((const uint8_t *)bad, strlen(bad), 0);
    TEST_ASSERT_EQUAL(ALC_IDLE, C.state);
    TEST_ASSERT_NOT_NULL(strstr((const char *)F.wr, "400"));
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_FALSE(F.closed_was_authed);
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_wrong_version_gets_426(void)
{
    openConn(0);
    // REQ with version 8 instead of 13: RFC 6455 4.4 wants 426 + Sec-WebSocket-Version: 13
    char req[sizeof(REQ) + 8];
    snprintf(req, sizeof(req), "%s", REQ);
    char *v = strstr(req, "Sec-WebSocket-Version: 13");
    TEST_ASSERT_NOT_NULL(v);
    memcpy(v + strlen("Sec-WebSocket-Version: "), " 8", 2);
    rx((const uint8_t *)req, strlen(req), 0);
    TEST_ASSERT_EQUAL(ALC_IDLE, C.state);
    TEST_ASSERT_NOT_NULL(strstr((const char *)F.wr, "426"));
    TEST_ASSERT_NOT_NULL(strstr((const char *)F.wr, "Sec-WebSocket-Version: 13"));
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_wrong_mac(void)
{
    openConn(0);
    rxHttp(0);
    F.wr_len = 0;
    sendAuth("wrong", 1);
    TEST_ASSERT_EQUAL(ALC_IDLE, C.state);
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_TRUE(srvTextHas(&s, "\"code\":\"auth\""));
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_EQUAL(AL_OP_CLOSE, s.op);
    TEST_ASSERT_EQUAL(4401, (s.p[0] << 8) | s.p[1]);
    TEST_ASSERT_EQUAL(4401, C.st.last_close);
    TEST_ASSERT_EQUAL(1, C.st.auth_fail);
    TEST_ASSERT_EQUAL(1, L.n);
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_lockout(void)
{
    openConn(0);
    for (int i = 0; i < 5; i++)
    {
        if (i)
            reopen(i * 100);
        rxHttp(i * 100);
        sendAuth("wrong", i * 100);
        TEST_ASSERT_EQUAL(ALC_IDLE, C.state);
    }
    reopen(600);
    rxHttp(600);
    F.wr_len = 0;
    sendAuth("secret", 600);  // right password but locked
    TEST_ASSERT_FALSE(alConnAuthed(&C));
    TEST_ASSERT_EQUAL(4429, C.st.last_close);
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_TRUE(srvTextHas(&s, "locked"));
}

static void test_binary_before_login(void)
{
    openConn(0);
    rxHttp(0);
    uint8_t p[2] = {1, 2}, b[16];
    rx(b, cframe(AL_OP_BIN, p, 2, b), 1);
    TEST_ASSERT_EQUAL(ALC_IDLE, C.state);
    TEST_ASSERT_EQUAL(4401, C.st.last_close);
    TEST_ASSERT_EQUAL(0, F.frames);
}

static void test_empty_password(void)
{
    openConn(0);
    AlConnInfo e = {"A", "f", "h", "   "};
    rxHttp(0);
    uint8_t m[32] = {0}, b[200];
    char hex[65], j[100];
    hexLower(m, 32, hex);
    snprintf(j, sizeof(j), "{\"t\":\"auth\",\"mac\":\"%s\"}", hex);
    alConnRx(&C, b, ctext(j, b), &e, &L, 1);
    TEST_ASSERT_EQUAL(4403, C.st.last_close);
    TEST_ASSERT_EQUAL(0, L.n);
}

static void test_login_timeout(void)
{
    openConn(100);
    alConnPoll(&C, 100 + APPLINK_LOGIN_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(alConnActive(&C));
    alConnPoll(&C, 100 + APPLINK_LOGIN_TIMEOUT_MS);
    TEST_ASSERT_FALSE(alConnActive(&C));
    TEST_ASSERT_EQUAL(4408, C.st.last_close);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    // also in the CHAL phase, with a close frame
    openConn(0);
    rxHttp(0);
    F.wr_len = 0;
    alConnPoll(&C, APPLINK_LOGIN_TIMEOUT_MS);
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_EQUAL(AL_OP_CLOSE, s.op);
    TEST_ASSERT_EQUAL(4408, (s.p[0] << 8) | s.p[1]);
}

static void test_silence_and_hb(void)
{
    openConn(0);
    login(0);
    alConnPoll(&C, APPLINK_HB_INTERVAL_MS - 1);
    TEST_ASSERT_EQUAL(0, F.wr_len);
    alConnPoll(&C, APPLINK_HB_INTERVAL_MS);
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_TRUE(srvTextHas(&s, "{\"t\":\"hb\"}"));
    // answering hb keeps the session alive
    rxText("{\"t\":\"hb\"}", 20000);
    alConnPoll(&C, 30000);
    alConnPoll(&C, 20000 + APPLINK_SILENCE_MS - 1);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
    alConnPoll(&C, 20000 + APPLINK_SILENCE_MS);
    TEST_ASSERT_FALSE(alConnActive(&C));
    TEST_ASSERT_EQUAL(4408, C.st.last_close);
    TEST_ASSERT_TRUE(F.closed_was_authed);
}

static void test_ping_pong(void)
{
    openConn(0);
    login(0);
    uint8_t p[3] = {'a', 'b', 'c'}, b[32];
    rx(b, cframe(AL_OP_PING, p, 3, b), 1);
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_EQUAL(AL_OP_PONG, s.op);
    TEST_ASSERT_EQUAL(3, s.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(p, s.p, 3);
    rx(b, cframe(AL_OP_PONG, p, 3, b), 2);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
}

static void test_client_close(void)
{
    openConn(0);
    login(0);
    uint8_t p[2] = {0x03, 0xe8}, b[16];  // 1000
    rx(b, cframe(AL_OP_CLOSE, p, 2, b), 1);
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_EQUAL(AL_OP_CLOSE, s.op);
    TEST_ASSERT_EQUAL(1000, (s.p[0] << 8) | s.p[1]);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_TRUE(F.closed_was_authed);
    TEST_ASSERT_FALSE(alConnActive(&C));
}

static void test_send_frame(void)
{
    openConn(0);
    uint8_t fr[100];
    memset(fr, 7, sizeof(fr));
    TEST_ASSERT_EQUAL(BLE_SEND_DOWN, alConnSendFrame(&C, fr, 100, 0));
    rxHttp(0);
    TEST_ASSERT_EQUAL(BLE_SEND_DOWN, alConnSendFrame(&C, fr, 100, 0));
    sendAuth("secret", 0);
    F.wr_len = 0;
    TEST_ASSERT_EQUAL(BLE_SEND_SENT, alConnSendFrame(&C, fr, 100, 0));
    TEST_ASSERT_EQUAL(102, F.wr_len);
    TEST_ASSERT_EQUAL(1, C.st.frames_out);
    // would-block: fill the buffer
    F.budget = 0;
    int sent = 0;
    while (alConnSendFrame(&C, fr, 100, 0) == BLE_SEND_SENT)
        sent++;
    TEST_ASSERT_EQUAL(5, sent);  // 5 * 102 = 510 fits in 512, a sixth does not
    TEST_ASSERT_EQUAL(1, C.st.busy);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
    F.budget = -1;
    alConnPoll(&C, 1);
    TEST_ASSERT_EQUAL(BLE_SEND_SENT, alConnSendFrame(&C, fr, 100, 1));
}

static void test_partial_write_pong_pending(void)
{
    openConn(0);
    login(0);
    uint8_t fr[200], b[32], p[1] = {9};
    memset(fr, 1, sizeof(fr));
    F.budget = 0;
    TEST_ASSERT_EQUAL(BLE_SEND_SENT, alConnSendFrame(&C, fr, 200, 0));
    TEST_ASSERT_EQUAL(BLE_SEND_SENT, alConnSendFrame(&C, fr, 200, 0));  // 408 queued
    rx(b, cframe(AL_OP_PING, p, 1, b), 1);                              // pong (3 B) fits
    F.budget = 5;
    alConnPoll(&C, 2);
    F.budget = -1;
    alConnPoll(&C, 3);
    TEST_ASSERT_EQUAL(204 + 204 + 3, F.wr_len);
}

static void test_io_dead(void)
{
    openConn(0);
    login(0);
    F.dead = true;
    uint8_t fr[10] = {0};
    alConnSendFrame(&C, fr, 10, 0);
    TEST_ASSERT_FALSE(alConnActive(&C));
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_TRUE(F.closed_was_authed);
    TEST_ASSERT_EQUAL(0, F.wr_len);  // no close frame
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_close_reserved_code_sends_no_close_frame(void)
{
    // RFC 6455 7.4.1: 1005/1006/1015 must never be sent on the wire. The glue
    // uses 1006 for a dead socket: the engine closes without writing anything.
    openConn(0);
    login(0);
    size_t w = F.wr_len;
    alConnClose(&C, 1006, "dead");
    TEST_ASSERT_EQUAL(ALC_IDLE, C.state);
    TEST_ASSERT_EQUAL(w, F.wr_len);
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_EQUAL(1006, C.st.last_close);
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_close_idempotent_no_callbacks_after(void)
{
    openConn(0);
    login(0);
    alConnClose(&C, 1001, "bye now");
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_TRUE(srvTextHas(&s, "\"t\":\"bye\",\"reason\":\"bye now\""));
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_EQUAL(AL_OP_CLOSE, s.op);
    TEST_ASSERT_EQUAL(1001, (s.p[0] << 8) | s.p[1]);
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    size_t w = F.wr_len;
    alConnClose(&C, 1000, NULL);
    uint8_t fr[4] = {0}, b[64];
    alConnPoll(&C, 99999999);
    rx(b, cframe(AL_OP_BIN, fr, 4, b), 5);
    rx((const uint8_t *)REQ, strlen(REQ), 5);
    TEST_ASSERT_EQUAL(BLE_SEND_DOWN, alConnSendFrame(&C, fr, 4, 0));
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_EQUAL(w, F.wr_len);
    TEST_ASSERT_EQUAL(0, F.frames);
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_close_inside_onframe_stops_rx_loop(void)
{
    // "--applink off via phone" / BLE takeover: the first onFrame closes the connection.
    // The second frame of the same rx call must not reach onFrame (io.close already ran).
    openConn(0);
    login(0);
    F.close_in_frame = 4409;
    uint8_t ph[4] = {0x04, 0x10, 0x20, 0x30}, b[128];
    size_t n = cframe(AL_OP_BIN, ph, 4, b);
    n += cframe(AL_OP_BIN, ph, 4, b + n);
    rx(b, n, 10);
    TEST_ASSERT_EQUAL(1, F.frames);
    TEST_ASSERT_EQUAL(ALC_IDLE, C.state);
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_TRUE(F.closed_was_authed);
    TEST_ASSERT_EQUAL(4409, C.st.last_close);
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_io_dead_on_ok_flush_no_onauthed(void)
{
    // write dies exactly when the "ok" message is flushed: the connection is gone, so
    // onAuthed must not run (the glue would announce a session that does not exist)
    openConn(0);
    rxHttp(0);
    F.dead_on_ok = true;
    sendAuth("secret", 5);
    TEST_ASSERT_EQUAL(0, F.authed_calls);
    TEST_ASSERT_FALSE(alConnActive(&C));
    TEST_ASSERT_FALSE(alConnAuthed(&C));
    TEST_ASSERT_EQUAL(1, F.closes);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_EQUAL(0, F.violations);
}

static void test_io_dead_on_ok_flush_reports_not_authed(void)
{
    // same case, the glue's view: onAuthed never ran, so onClosed must say wasAuthed=false
    // (else the glue ends a phone session that was never started)
    openConn(0);
    rxHttp(0);
    F.dead_on_ok = true;
    sendAuth("secret", 5);
    TEST_ASSERT_EQUAL(1, F.closed_calls);
    TEST_ASSERT_FALSE(F.closed_was_authed);
}

static void test_millis_wrap(void)
{
    uint32_t t0 = 0xFFFFFFF0u;
    openConn(t0);
    login(t0);
    alConnPoll(&C, t0 + APPLINK_HB_INTERVAL_MS);  // wraps past 0
    Srv s;
    srvStart(&s, false);
    TEST_ASSERT_TRUE(srvNext(&s));
    TEST_ASSERT_TRUE(srvTextHas(&s, "hb"));
    alConnPoll(&C, t0 + APPLINK_SILENCE_MS - 1);
    TEST_ASSERT_TRUE(alConnAuthed(&C));
    alConnPoll(&C, t0 + APPLINK_SILENCE_MS);
    TEST_ASSERT_FALSE(alConnActive(&C));
    // login timeout across the wrap
    openConn(t0);
    alConnPoll(&C, t0 + APPLINK_LOGIN_TIMEOUT_MS - 1);
    TEST_ASSERT_TRUE(alConnActive(&C));
    alConnPoll(&C, t0 + APPLINK_LOGIN_TIMEOUT_MS);
    TEST_ASSERT_FALSE(alConnActive(&C));
}

static void test_decoder_error(void)
{
    openConn(0);
    login(0);
    uint8_t b[8] = {0x80 | 0x3, 0x80, 0, 0, 0, 0};  // reserved opcode 3
    rx(b, 6, 1);
    TEST_ASSERT_FALSE(alConnActive(&C));
    TEST_ASSERT_EQUAL(1002, C.st.last_close);
}

static void test_sizeof(void)
{
    char m[64];
    snprintf(m, sizeof(m), "sizeof(AlConn)=%u", (unsigned)sizeof(AlConn));
    TEST_MESSAGE(m);
    printf("%s\n", m);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_happy_path);
    RUN_TEST(test_http_one_byte_chunks);
    RUN_TEST(test_http_plus_ws_frame);
    RUN_TEST(test_bad_request);
    RUN_TEST(test_wrong_version_gets_426);
    RUN_TEST(test_wrong_mac);
    RUN_TEST(test_lockout);
    RUN_TEST(test_binary_before_login);
    RUN_TEST(test_empty_password);
    RUN_TEST(test_login_timeout);
    RUN_TEST(test_silence_and_hb);
    RUN_TEST(test_ping_pong);
    RUN_TEST(test_client_close);
    RUN_TEST(test_send_frame);
    RUN_TEST(test_partial_write_pong_pending);
    RUN_TEST(test_io_dead);
    RUN_TEST(test_close_idempotent_no_callbacks_after);
    RUN_TEST(test_close_reserved_code_sends_no_close_frame);
    RUN_TEST(test_close_inside_onframe_stops_rx_loop);
    RUN_TEST(test_io_dead_on_ok_flush_no_onauthed);
    RUN_TEST(test_io_dead_on_ok_flush_reports_not_authed);
    RUN_TEST(test_millis_wrap);
    RUN_TEST(test_decoder_error);
    RUN_TEST(test_sizeof);
    return UNITY_END();
}
