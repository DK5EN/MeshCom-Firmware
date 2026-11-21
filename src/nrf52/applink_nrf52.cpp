#include "applink.h"

#if defined(HAS_APPLINK) && defined(NRF52_SERIES)

// AppLink on the RAK4631 + RAK13800 (W5100S): one TCP listener on :8002, one session.
// Contract: docs/applink-api-contract.md. Engine: applink_conn.h (shared with ESP32).
//
// Socket handling: the library's socket*() calls are private (friends only), so the
// glue uses
//   - EthernetServer::begin() ONCE to open + LISTEN a socket (it does not re-listen;
//     only available()/accept() do), then finds the index in EthernetServer::server_port[]
//   - EthernetClient(index) for status/available/read/write/availableForWrite
//   - W5100.execCmdSn() for the non-blocking close (EthernetClient::stop() waits up to 1 s)
// While a session is up no listener is reopened (contract 6: one socket for AppLink).
//
// N-20: nothing here touches the W5100S unless neth.hasIPaddress (checked first in every
// entry that reaches a socket). applinkApplySetting() only sets a flag.
//
// SPI: the library calls wrap their own SPI.beginTransaction; direct W5100 register
// access below is wrapped here. bSPI_ETH_Active is held by the loop hook around
// applinkLoop(); the other entry points (close, send) take it themselves (SpiGuard).

#include <Arduino.h>
#include <SPI.h>
#include <string.h>
#include <RAK13800_W5100S.h>
#include <w5100.h>
#include "eth_mss.h"   // shared with webApplyEthMss (#1183)
#include <nrf_eth.h>
#include <nrf_soc.h>   // sd_rand_application_*
#include <nrf_sdm.h>   // sd_softdevice_is_enabled
#include <nrf52_radio.h>   // startRadioReceive
#include <loop_functions_extern.h>
#include <phone_commands.h>
#include "applink_auth.h"
#include "nbr_views.h"      // nbrHardwareName()
#include "applink_conn.h"

extern NrfETH neth;   // nrf52_main.cpp

#define AL_PORT        8002
#define AL_RX_CHUNK    512
#define AL_RETRY_MS    5000UL
#define AL_STUCK_MS    10000UL   // non-LISTEN, non-ESTABLISHED state this long -> recycle
#define AL_NONCE_MS    5000UL    // no random bytes for this long after accept -> drop

static void applinkBindIo(AlConnIo *io);

static AlConn s_conn;
static AlLockout s_lock;
static bool s_inited = false;
static char s_pw[16] = {0};
static uint8_t s_sock = 0xFF;      // socket index, 0xFF = none
static uint8_t s_lastSr = 0;
static uint32_t s_srSince = 0;
static uint32_t s_nextOpen = 0;    // millis() gate for listener (re)open
static bool s_gotNoSockLog = false;
static uint32_t s_lastNoSockLog = 0;
static uint8_t s_peer[4] = {0, 0, 0, 0};
static uint32_t s_upSince = 0;
static uint8_t s_rxbuf[AL_RX_CHUNK];
// PHY link, read ONCE per applinkLoop pass (Ethernet.linkStatus(): one SPI register read).
// alIoWrite refuses while it is false: EthernetClient::write() -> socketSend() spins on
// SEND_OK without a bound, so a pulled cable (hasIPaddress still true) would block the loop.
static bool s_linkUp = false;
static bool s_staleSweep = false;   // IP lost with a chip socket possibly still open on 8002

static EthernetServer s_srv(AL_PORT);   // used for begin() only

// --- SPI guard for entry points outside the loop hook --------------------------

struct SpiGuard
{
    bool prev;
    SpiGuard() : prev(bSPI_ETH_Active) { bSPI_ETH_Active = true; }
    ~SpiGuard()
    {
        bSPI_ETH_Active = prev;
        if (!prev && bPendingRadioRx)
        {
            bPendingRadioRx = false;
            startRadioReceive();
        }
    }
};

// --- helpers -------------------------------------------------------------------

static bool alWanted()
{
    return bAPPLINK && !bEXTUDP && s_pw[0] != 0 && neth.hasIPaddress;
}

static void alEnsureInit()
{
    if (s_inited)
        return;
    AlConnIo io;
    memset(&io, 0, sizeof(io));
    applinkBindIo(&io);
    alLockoutInit(&s_lock);
    alConnInit(&s_conn, &io);
    s_inited = true;
}

// MSS: same rule as webApplyEthMss (web_functions.cpp): Sn_MSSR is latched at OPEN, so
// pre-write node_ethmtu-40 to every CLOSED socket before EthernetServer::begin().
static void alPrewriteMss()
{
    const uint8_t chip = W5100.getChip();
    if (!chip)
        return;
    ethPrewriteClosedMss(ethMssTarget(meshcom_settings.node_ethmtu), ethSockCount(chip));
}

// true if at least one socket is CLOSED (EthernetServer::begin() would otherwise
// force-close another user's LAST_ACK/TIME_WAIT socket as its last resort)
static uint8_t alSockCount()
{
    uint8_t nsock = MAX_SOCK_NUM;
    if (nsock > 4 && (W5100.getChip() == 50 || W5100.getChip() == 51))
        nsock = 4;
    return nsock;
}

static bool alHasFreeSocket(uint8_t nsock)
{
    bool free = false;
    SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
    for (uint8_t i = 0; i < nsock && !free; i++)
        free = (W5100.readSnSR(i) == SnSR::CLOSED);
    SPI.endTransaction();
    return free;
}

static void alNoSockLog(uint32_t now)
{
    if (!s_gotNoSockLog || (uint32_t)(now - s_lastNoSockLog) > 30000UL)
    {
        Serial.printf("[APPLINK] no free socket, retry\n");
        s_gotNoSockLog = true;
        s_lastNoSockLog = now;
    }
}

// A listener from before an IP loss may still sit in the chip (the chip is not always reset
// before the IP returns, nrf_eth.cpp). It would accept a client that nobody serves, and a
// second LISTEN on the same port would be opened next to it. Close every socket that is
// bound to AL_PORT and is not ours, and forget its EthernetServer::server_port[] claim.
// Called only with IP, and only while s_sock == 0xFF (no session socket to protect).
static void alCloseStaleListeners(uint8_t nsock)
{
    SpiGuard g;
    for (uint8_t i = 0; i < nsock; i++)
    {
        if (i == s_sock)
            continue;
        SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
        if (W5100.readSnPORT(i) == AL_PORT && W5100.readSnSR(i) != SnSR::CLOSED)
        {
            W5100.execCmdSn(i, Sock_DISCON);
            W5100.execCmdSn(i, Sock_CLOSE);
        }
        SPI.endTransaction();
        if (EthernetServer::server_port[i] == AL_PORT)
            EthernetServer::server_port[i] = 0;
    }
}

static void alOpenListener(uint32_t now)
{
    if (!W5100.getChip())
    {
        s_nextOpen = now + AL_RETRY_MS;
        return;
    }
    const uint8_t nsock = alSockCount();
    alCloseStaleListeners(nsock);   // first: a stale listener may be what fills the chip
    s_staleSweep = false;
    if (!alHasFreeSocket(nsock))
    {
        alNoSockLog(now);
        s_nextOpen = now + AL_RETRY_MS;
        return;
    }
    alPrewriteMss();
    s_srv.begin();
    uint8_t found = 0xFF;
    for (uint8_t i = 0; i < nsock; i++)
    {
        if (EthernetServer::server_port[i] == AL_PORT)
        {
            EthernetClient c(i);
            if (c.status() == SnSR::LISTEN)
            {
                found = i;
                break;
            }
        }
    }
    if (found == 0xFF)
    {
        alNoSockLog(now);
        s_nextOpen = now + AL_RETRY_MS;
        return;
    }
    s_gotNoSockLog = false;
    s_sock = found;
    s_lastSr = SnSR::LISTEN;
    s_srSince = now;
}

// RAM-only: is s_sock still ours? A session socket that went CLOSED (peer reset, TCP timeout) can be handed out
// again by Ethernet.socketBegin() -- web server available(), gateway UDP, DHCP, NTP -- in the same loop pass, and
// socketBegin() zeroes EthernetServer::server_port[s] for the slot it takes. Our claim is AL_PORT, so a lost claim
// means the slot belongs to someone else now: never read, write, DISCON or CLOSE it (advisor finding 2026-11-21).
static bool alOwnsSock()
{
    return s_sock != 0xFF && EthernetServer::server_port[s_sock] == AL_PORT;
}

// non-blocking: FIN (DISCON), then immediate CLOSE; frees the slot for a new listener
static void alCloseSocket()
{
    if (s_sock == 0xFF)
        return;
    const bool owned = alOwnsSock();   // a taken slot is forgotten, never touched
    if (owned && neth.hasIPaddress)   // N-20: no chip access without Ethernet
    {
        SpiGuard g;
        SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
        W5100.execCmdSn(s_sock, Sock_DISCON);
        W5100.execCmdSn(s_sock, Sock_CLOSE);
        SPI.endTransaction();
    }
    // without IP the chip socket stays open here; alOpenListener() closes it (by port) once
    // the IP is back, before any new listener is opened
    if (owned)
        EthernetServer::server_port[s_sock] = 0;   // only our own claim, never another user's
    s_sock = 0xFF;
    s_nextOpen = 0;   // reopen on the next pass
}

static bool alNonce(uint8_t out[16])
{
    uint8_t en = 0;
    sd_softdevice_is_enabled(&en);
    if (!en)
        return false;
    uint8_t avail = 0;
    sd_rand_application_bytes_available_get(&avail);
    if (avail < 16)
        return false;
    return sd_rand_application_vector_get(out, 16) == NRF_SUCCESS;
}

static void alReadPeer()
{
    SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
    W5100.readSnDIPR(s_sock, s_peer);
    SPI.endTransaction();
}

// --- AlConnIo callbacks ----------------------------------------------------------

static int alIoWrite(void *, const uint8_t *buf, size_t len)
{
    // N-20 + unbounded socketSend(): no chip access without IP or with the cable out
    if (!alOwnsSock() || !neth.hasIPaddress || !s_linkUp)   // taken slot: fail, the engine closes (1006)
        return -1;
    SpiGuard g;
    EthernetClient c(s_sock);
    int avail = c.availableForWrite();
    if (avail <= 0)
    {
        const uint8_t sr = c.status();
        return (sr == SnSR::ESTABLISHED || sr == SnSR::CLOSE_WAIT) ? 0 : -1;
    }
    size_t n = len < (size_t)avail ? len : (size_t)avail;
    if (n == 0)
        return 0;
    return (int)c.write(buf, n) > 0 ? (int)n : -1;
}

static void alIoClose(void *)
{
    alCloseSocket();
}

static void alIoOnFrame(void *, uint8_t *frame, size_t len)
{
    // static, not on the 4 KB nRF52 loop stack: the glue runs only in the loop task and
    // onFrame never nests (readPhoneCommand does not call back into alConnRx), so no re-entry
    static uint8_t buf[MAX_MSG_LEN_PHONE];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, frame, len < sizeof(buf) ? len : sizeof(buf));
    readPhoneCommand(buf, PL_NET);
}

static void alIoOnAuthed(void *)
{
    s_upSince = millis();
    Serial.printf("[APPLINK] session up %u.%u.%u.%u\n", s_peer[0], s_peer[1], s_peer[2], s_peer[3]);
}

static void alIoOnClosed(void *, bool wasAuthed)
{
    if (!wasAuthed)
        return;   // login never completed: no phone session to end
    Serial.printf("[APPLINK] session closed\n");
    phoneSessionEnded(PL_NET);
}

static void applinkBindIo(AlConnIo *io)
{
    io->write = alIoWrite;
    io->close = alIoClose;
    io->onFrame = alIoOnFrame;
    io->onAuthed = alIoOnAuthed;
    io->onClosed = alIoOnClosed;
    io->ctx = nullptr;
}

// --- public API ------------------------------------------------------------------

void applinkSetPassword(const char *pw)
{
    size_t n = pw ? strlen(pw) : 0;
    while (n > 0 && pw[n - 1] == ' ')
        n--;
    if (n >= sizeof(s_pw))
        n = sizeof(s_pw) - 1;
    if (n)
        memcpy(s_pw, pw, n);
    s_pw[n] = 0;
    if (n == 0 && s_inited && alConnActive(&s_conn))
        alConnClose(&s_conn, 4403, "password removed");
}

void applinkApplySetting()
{
    // N-20: no socket access here (boot runs before Ethernet is up); applinkLoop() acts
    s_nextOpen = 0;
}

void applinkClose(uint16_t code, const char *reason)
{
    if (!s_inited)
        return;
    if (alConnActive(&s_conn))
    {
        SpiGuard g;
        alConnClose(&s_conn, code, reason);
    }
}

// ETH-06: see applink.h. Order matters: s_sock is forgotten first, so the session close below
// (alIoClose -> alCloseSocket) has no index left to DISCON/CLOSE on the chip.
void applinkChipReset()
{
    if (s_sock != 0xFF)
    {
        if (alOwnsSock())
            EthernetServer::server_port[s_sock] = 0;   // only our own claim
        s_sock = 0xFF;
        Serial.printf("[APPLINK];event;chip_reset;socket;dropped;ms;%lu\n", (unsigned long)millis());
    }
    s_staleSweep = false;   // the reset closed every chip socket: nothing stale to sweep
    if (s_inited && alConnActive(&s_conn))
        alConnClose(&s_conn, 1006, NULL);   // 1006: no close frame on the wire
    s_nextOpen = 0;   // new listener on the first pass with IP and link
}

bool applinkSessionUp()
{
    return s_inited && alConnAuthed(&s_conn);
}

BlePhoneSend applinkSendFrame(const uint8_t *buf, uint16_t len)
{
    if (!s_inited || !alConnAuthed(&s_conn))
        return BLE_SEND_DOWN;
    return alConnSendFrame(&s_conn, buf, len, millis());
}

size_t applinkStatus(char *out, size_t out_size)
{
    if (!out || !out_size)
        return 0;
    int n;
    if (!bAPPLINK)
        n = snprintf(out, out_size, "AppLink off");
    else if (bEXTUDP)
        n = snprintf(out, out_size, "AppLink off (ext-UDP on)");
    else if (s_pw[0] == 0)
        n = snprintf(out, out_size, "AppLink off (no password)");
    else if (!neth.hasIPaddress)
        n = snprintf(out, out_size, "AppLink on, waiting for Ethernet");
    else if (s_inited && alConnAuthed(&s_conn))
        n = snprintf(out, out_size, "AppLink on :%d session %u.%u.%u.%u up %lus in %lu out %lu", AL_PORT, s_peer[0],
                     s_peer[1], s_peer[2], s_peer[3], (unsigned long)((uint32_t)(millis() - s_upSince) / 1000UL),
                     (unsigned long)s_conn.st.frames_in, (unsigned long)s_conn.st.frames_out);
    else if (s_inited && alConnActive(&s_conn))
        n = snprintf(out, out_size, "AppLink on :%d login from %u.%u.%u.%u", AL_PORT, s_peer[0], s_peer[1], s_peer[2],
                     s_peer[3]);
    else if (s_sock == 0xFF)
        n = snprintf(out, out_size, "AppLink on :%d no free socket", AL_PORT);
    else
        n = snprintf(out, out_size, "AppLink on :%d listening", AL_PORT);
    if (n < 0)
        n = 0;
    if ((size_t)n >= out_size)
        n = (int)out_size - 1;
    return (size_t)n;
}

void applinkLoop()
{
    // N-20: no W5100S access without an initialised, addressed Ethernet
    if (!neth.hasIPaddress)
    {
        // network gone: the chip state is not ours to read. 1006 = dead socket, the engine
        // writes no close frame (alIoWrite would refuse anyway). alCloseSocket() skips the chip
        // and forgets the socket; alOpenListener() closes the stale chip socket later.
        s_linkUp = false;
        if (s_sock != 0xFF)
            s_staleSweep = true;   // the chip may still hold it in LISTEN/ESTABLISHED
        if (s_inited && alConnActive(&s_conn))
            alConnClose(&s_conn, 1006, NULL);
        alCloseSocket();
        return;
    }

    s_linkUp = (Ethernet.linkStatus() == LinkON);

    const uint32_t now = millis();
    alEnsureInit();

    if (!s_linkUp)
    {
        // cable out while hasIPaddress is still true: a write would block in socketSend()
        if (alConnActive(&s_conn))
            alConnClose(&s_conn, 1006, NULL);   // no wire write; alIoClose frees the socket
        return;
    }

    if (!alWanted())
    {
        if (alConnActive(&s_conn))
            alConnClose(&s_conn, 1001, NULL);
        alCloseSocket();
        // AppLink was switched off (or lost its password / ext-UDP took over) while the IP was
        // gone: the forgotten chip socket on 8002 is not reached by alOpenListener()'s sweep.
        // IP and link are up here, so the chip may be touched; once is enough.
        if (s_staleSweep && W5100.getChip())
        {
            alCloseStaleListeners(alSockCount());
            s_staleSweep = false;
        }
        return;
    }

    if (s_sock == 0xFF)
    {
        if (s_nextOpen == 0 || (int32_t)(now - s_nextOpen) >= 0)
            alOpenListener(now);
        if (s_sock == 0xFF)
        {
            alConnPoll(&s_conn, now);
            return;
        }
    }

    if (!alOwnsSock())
    {
        // our CLOSED slot was handed to another socket user earlier in this pass: forget it, end the session
        Serial.printf("[APPLINK];event;socket_taken;ms;%lu\n", (unsigned long)now);
        s_sock = 0xFF;
        s_nextOpen = 0;
        if (alConnActive(&s_conn))
            alConnClose(&s_conn, 1006, NULL);   // alIoClose -> alCloseSocket: s_sock already gone, no chip access
        alConnPoll(&s_conn, now);
        return;
    }

    EthernetClient cl(s_sock);
    const uint8_t sr = cl.status();
    if (sr != s_lastSr)
    {
        s_lastSr = sr;
        s_srSince = now;
    }

    const bool active = alConnActive(&s_conn);

    if (sr == SnSR::LISTEN)
    {
        if (active)   // our session socket fell back to LISTEN: dead
            alConnClose(&s_conn, 1006, NULL);
    }
    else if (sr == SnSR::ESTABLISHED || sr == SnSR::CLOSE_WAIT)
    {
        if (!active)
        {
            if (sr == SnSR::CLOSE_WAIT)
            {
                alCloseSocket();
            }
            else
            {
                uint8_t nonce[16];
                if (alNonce(nonce))
                {
                    alReadPeer();
                    alConnOpen(&s_conn, nonce, now);
                    memset(nonce, 0, sizeof(nonce));
                }
                else if ((uint32_t)(now - s_srSince) > AL_NONCE_MS)
                {
                    alCloseSocket();
                }
            }
        }
        else
        {
            int avail = cl.available();
            if (avail > 0)
            {
                int got = cl.read(s_rxbuf, avail < AL_RX_CHUNK ? (size_t)avail : (size_t)AL_RX_CHUNK);
                if (got > 0)
                {
                    AlConnInfo info;
                    char fw[16];
                    char hw[24];
                    snprintf(fw, sizeof(fw), "%s%s", SOURCE_VERSION, SOURCE_VERSION_SUB);   // "4.40a", as ESP32
                    alHwLabel(nbrHardwareName(BOARD_HARDWARE), BOARD_HARDWARE, hw, sizeof(hw));   // contract: name, else number
                    info.call = meshcom_settings.node_call;
                    info.fw = fw;
                    info.hw = hw;
                    info.password = s_pw;
                    alConnRx(&s_conn, s_rxbuf, (size_t)got, &info, &s_lock, now);
                }
            }
            if (alConnActive(&s_conn) && sr == SnSR::CLOSE_WAIT && cl.available() <= 0)
                alConnClose(&s_conn, 1001, NULL);
        }
    }
    else
    {
        // CLOSED, FIN_WAIT, LAST_ACK, CLOSING, TIME_WAIT, INIT, SYNRECV ...
        if (active)
            alConnClose(&s_conn, 1006, NULL);
        else if (sr == SnSR::CLOSED || (uint32_t)(now - s_srSince) > AL_STUCK_MS)
            alCloseSocket();
    }

    alConnPoll(&s_conn, now);
}

#endif   // HAS_APPLINK && NRF52_SERIES
