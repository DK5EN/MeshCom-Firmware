// AppLink ESP32 platform glue: TCP server on port 8002 (raw lwIP sockets, nonblocking, loop task only)
// on top of the shared connection engine applink_conn.h. Modelled on kiss_functions.cpp.
// Contract: docs/applink-api-contract.md sections 5-8.
#include "applink.h"

#if defined(HAS_APPLINK) && defined(ESP32)

#include <Arduino.h>
#include <WiFi.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>   // TCP_NODELAY
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <esp_random.h>

#include <loop_functions.h>   // configuration_global.h before the extern header (MAX_RING, UDP_TX_BUF_SIZE)
#include <loop_functions_extern.h>
#include <net_console.h>   // Serial -> MSerial

#include "applink_auth.h"
#include "nbr_views.h"      // nbrHardwareName()
#include "applink_conn.h"
#include "applink_ws.h"
#include "configuration_global.h"   // SOURCE_VERSION, MAX_MSG_LEN_PHONE
#include "meshcom_settings.h"
#include "phone_commands.h"

#define AL_SLOTS 2

static int s_listen_fd = -1;
static AlConn s_conn[AL_SLOTS];
static int s_fd[AL_SLOTS] = {-1, -1};
static char s_ip[AL_SLOTS][16];
static uint32_t s_authedAt[AL_SLOTS];
static int s_sessionIdx = -1;   // slot holding the authenticated session, -1 none
static AlLockout s_lock;
static char s_pw[15];
static bool s_inited = false;
static uint32_t s_retryAt = 0;
static uint32_t s_logAt = 0;
static uint8_t s_rx[APPLINK_OUT_BUF];

// ---------------------------------------------------------------------------
static void alLog(const char *msg)
{
    uint32_t now = millis();
    if (s_logAt != 0 && (int32_t)(now - s_logAt) < 0)
        return;
    s_logAt = now + 2000;
    Serial.printf("[APPLINK] %s\n", msg);
}

static int ioWrite(void *ctx, const uint8_t *buf, size_t len)
{
    int i = (int)(intptr_t)ctx;
    if (i < 0 || i >= AL_SLOTS || s_fd[i] < 0)
        return -1;
    int r = ::send(s_fd[i], buf, len, MSG_DONTWAIT);
    if (r >= 0)
        return r;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return 0;
    return -1;
}

static void ioClose(void *ctx)
{
    int i = (int)(intptr_t)ctx;
    if (i < 0 || i >= AL_SLOTS || s_fd[i] < 0)
        return;
    ::close(s_fd[i]);
    s_fd[i] = -1;
}

static void ioOnFrame(void *ctx, uint8_t *frame, size_t len)
{
    (void)ctx;
    uint8_t buf[MAX_MSG_LEN_PHONE];
    memset(buf, 0, sizeof(buf));
    if (len > sizeof(buf))
        len = sizeof(buf);
    memcpy(buf, frame, len);
    readPhoneCommand(buf, PL_NET);
}

static void ioOnAuthed(void *ctx)
{
    int i = (int)(intptr_t)ctx;
    if (i < 0 || i >= AL_SLOTS)
        return;
    // Close the old session FIRST: its onClosed ends the phone session
    // (phoneSessionEnded resets isPhoneReady and the config flags), so the new
    // client receives no frames before its own hello re-claims PL_NET.
    int old = s_sessionIdx;
    if (old >= 0 && old != i && s_fd[old] >= 0)
        alConnClose(&s_conn[old], 4409, "taken over");
    s_sessionIdx = i;
    s_authedAt[i] = millis();
}

static void ioOnClosed(void *ctx, bool wasAuthed)
{
    int i = (int)(intptr_t)ctx;
    (void)wasAuthed;
    if (i < 0 || i >= AL_SLOTS)
        return;
    s_fd[i] = -1;
    if (s_sessionIdx == i)
    {
        s_sessionIdx = -1;
        phoneSessionEnded(PL_NET);
    }
}

static void makeIo(int i, AlConnIo *io)
{
    memset(io, 0, sizeof(*io));
    io->write = ioWrite;
    io->close = ioClose;
    io->onFrame = ioOnFrame;
    io->onAuthed = ioOnAuthed;
    io->onClosed = ioOnClosed;
    io->ctx = (void *)(intptr_t)i;
}

static void slotsInit()
{
    if (s_inited)
        return;
    s_inited = true;
    alLockoutInit(&s_lock);
    for (int i = 0; i < AL_SLOTS; i++)
    {
        AlConnIo io;
        makeIo(i, &io);
        alConnInit(&s_conn[i], &io);
        s_fd[i] = -1;
    }
}

static void closeAll(uint16_t code, const char *reason)
{
    for (int i = 0; i < AL_SLOTS; i++)
        if (s_fd[i] >= 0)
            alConnClose(&s_conn[i], code, reason);
}

static void stopListener()
{
    if (s_listen_fd >= 0)
    {
        ::close(s_listen_fd);
        s_listen_fd = -1;
    }
}

static bool networkUp()
{
    wifi_mode_t mode = WiFi.getMode();
    return meshcom_settings.node_hasIPaddress || WiFi.status() == WL_CONNECTED ||
           mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA;
}

static bool wanted()
{
    return bAPPLINK && s_pw[0] != 0;
}

static void startListener()
{
    uint32_t now = millis();
    if (s_retryAt != 0 && (int32_t)(now - s_retryAt) < 0)
        return;
    s_retryAt = now + 5000;

    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        alLog("socket() failed, retry in 5s");
        return;
    }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(APPLINK_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || ::listen(fd, 2) < 0)
    {
        alLog("bind/listen failed, retry in 5s");
        ::close(fd);
        return;
    }
    int fl = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    s_listen_fd = fd;
    s_retryAt = 0;
    Serial.printf("[APPLINK] listening on :%d\n", APPLINK_PORT);
}

// ---------------------------------------------------------------------------
void applinkSetPassword(const char *pw)
{
    slotsInit();
    size_t n = 0;
    if (pw)
    {
        n = strnlen(pw, sizeof(s_pw) - 1);
        memcpy(s_pw, pw, n);
        while (n > 0 && s_pw[n - 1] == ' ')
            n--;
    }
    s_pw[n] = 0;
    if (n == 0)
    {
        closeAll(4403, "no password");
        stopListener();
    }
}

void applinkApplySetting()
{
    slotsInit();
    if (!wanted() || !networkUp())
    {
        closeAll(1001, wanted() ? "network down" : "off");
        stopListener();
        return;
    }
    if (s_listen_fd < 0)
    {
        s_retryAt = 0;
        startListener();
    }
}

static void acceptClients()
{
    for (int n = 0; n < 2; n++)
    {
        struct sockaddr_in cli;
        socklen_t clen = sizeof(cli);
        int fd = ::accept(s_listen_fd, (struct sockaddr *)&cli, &clen);
        if (fd < 0)
            return;
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        int slot = -1;
        for (int i = 0; i < AL_SLOTS; i++)
            if (s_fd[i] < 0)
            {
                slot = i;
                break;
            }
        if (slot < 0)
        {
            char rej[96];
            size_t rl = alHttpReject(503, rej, sizeof(rej));
            if (rl)
                ::send(fd, rej, rl, MSG_DONTWAIT);
            ::close(fd);
            alLog("third client refused");
            continue;
        }
        s_fd[slot] = fd;
        uint32_t a = cli.sin_addr.s_addr;
        snprintf(s_ip[slot], sizeof(s_ip[slot]), "%u.%u.%u.%u", (unsigned)(a & 0xFF),
                 (unsigned)((a >> 8) & 0xFF), (unsigned)((a >> 16) & 0xFF),
                 (unsigned)((a >> 24) & 0xFF));
        AlConnIo io;
        makeIo(slot, &io);
        alConnInit(&s_conn[slot], &io);   // reset state
        uint8_t nonce[16];
        esp_fill_random(nonce, sizeof(nonce));
        alConnOpen(&s_conn[slot], nonce, millis());
    }
}

void applinkLoop()
{
    slotsInit();
    bool want = wanted();
    bool up = want && networkUp();
    if (!up)
    {
        if (s_listen_fd >= 0 || s_fd[0] >= 0 || s_fd[1] >= 0)
        {
            closeAll(1001, want ? "network down" : "off");
            stopListener();
        }
        return;
    }
    if (s_listen_fd < 0)
    {
        startListener();
        if (s_listen_fd < 0)
            return;
    }

    acceptClients();

    AlConnInfo info;
    char fw[16];
    char hw[24];
    snprintf(fw, sizeof(fw), "%s%s", SOURCE_VERSION, SOURCE_VERSION_SUB);
    alHwLabel(nbrHardwareName(BOARD_HARDWARE), BOARD_HARDWARE, hw, sizeof(hw));   // contract: name, else number
    info.call = meshcom_settings.node_call;
    info.fw = fw;
    info.hw = hw;
    info.password = s_pw;

    uint32_t now = millis();
    for (int i = 0; i < AL_SLOTS; i++)
    {
        for (int k = 0; k < 2 && s_fd[i] >= 0; k++)
        {
            int r = ::recv(s_fd[i], s_rx, sizeof(s_rx), MSG_DONTWAIT);
            if (r > 0)
            {
                alConnRx(&s_conn[i], s_rx, (size_t)r, &info, &s_lock, now);
                if (r < (int)sizeof(s_rx))
                    break;
            }
            else if (r == 0)
            {
                alConnClose(&s_conn[i], 1001, NULL);
            }
            else
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    alConnClose(&s_conn[i], 1006, NULL);
                break;
            }
        }
        if (s_fd[i] >= 0)
            alConnPoll(&s_conn[i], now);
    }
}

void applinkClose(uint16_t code, const char *reason)
{
    if (s_sessionIdx >= 0 && s_fd[s_sessionIdx] >= 0)
        alConnClose(&s_conn[s_sessionIdx], code, reason);
}

bool applinkSessionUp()
{
    return s_sessionIdx >= 0 && s_fd[s_sessionIdx] >= 0 && alConnAuthed(&s_conn[s_sessionIdx]);
}

BlePhoneSend applinkSendFrame(const uint8_t *buf, uint16_t len)
{
    if (!applinkSessionUp())
        return BLE_SEND_DOWN;
    return alConnSendFrame(&s_conn[s_sessionIdx], buf, len, millis());
}

size_t applinkStatus(char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return 0;
    int n;
    if (!bAPPLINK)
        n = snprintf(out, out_size, "AppLink off");
    else if (s_pw[0] == 0)
        n = snprintf(out, out_size, "AppLink off (no password)");
    else if (!networkUp() || s_listen_fd < 0)
        n = snprintf(out, out_size, "AppLink on, waiting for network");
    else if (applinkSessionUp())
    {
        const AlConn *c = &s_conn[s_sessionIdx];
        n = snprintf(out, out_size,
                     "AppLink on :%d session %s up %lus in %lu out %lu busy %lu authfail %lu",
                     APPLINK_PORT, s_ip[s_sessionIdx],
                     (unsigned long)((millis() - s_authedAt[s_sessionIdx]) / 1000),
                     (unsigned long)c->st.frames_in, (unsigned long)c->st.frames_out,
                     (unsigned long)c->st.busy, (unsigned long)c->st.auth_fail);
    }
    else
        n = snprintf(out, out_size, "AppLink on :%d no session", APPLINK_PORT);
    if (n < 0)
        n = 0;
    if ((size_t)n >= out_size)
        n = (int)out_size - 1;
    return (size_t)n;
}

#endif
