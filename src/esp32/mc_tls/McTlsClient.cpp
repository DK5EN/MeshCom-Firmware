/*
  McTlsClient.cpp -- see McTlsClient.h (fork of Arduino-ESP32 2.0.17 WiFiClientSecure.cpp).
  Derived from WiFiClientSecure of Arduino-ESP32 2.0.17 (libraries/WiFiClientSecure):
  Copyright (c) 2016 Hristo Gochkov  All right reserved.
  Additions Copyright (C) 2017 Evandro Luis Copercini.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#if defined(ESP32)

#include "McTlsClient.h"
#include <esp_heap_caps.h>
#include "fw_update_roots.h"
#include "fw_tls_time.h"
#include "esp32/mc_dns.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <errno.h>

#undef connect
#undef write
#undef read

McTlsClient::McTlsClient()
{
    _connected = false;
    _timeout = 30000; // Same default as ssl_client

    sslclient = new mc_ssl_context;
    mc_ssl_init(sslclient);
    sslclient->handshake_timeout = 120000;
    _use_insecure = false;
    _CA_cert = NULL;
}

McTlsClient::~McTlsClient()
{
    stop();
    delete sslclient;
}

void McTlsClient::stop()
{
    if (sslclient->socket >= 0) {
        close(sslclient->socket);
        sslclient->socket = -1;
    }
    _connected = false;
    _peek = -1;
    mc_ssl_stop(sslclient);
}

int McTlsClient::connect(IPAddress ip, uint16_t port)
{
    // No host name: the selector cannot decide, an explicit CA or the full bundle applies.
    return connectTo(ip.toString().c_str(), ip, port, _CA_cert != NULL ? _CA_cert : FW_UPDATE_ROOTS_PEM);
}

int McTlsClient::connect(IPAddress ip, uint16_t port, int32_t timeout)
{
    _timeout = timeout;
    return connect(ip, port);
}

// Largest free block needed before the bundle retry: two TLS records (2 x 16717 B) plus margin.
#define MC_TLS_RETRY_MIN_BLK 40000u

int McTlsClient::connect(const char *host, uint16_t port)
{
    // B2: per connect, so an HTTPClient redirect to another host selects again.
    const char *ca = _CA_cert != NULL ? _CA_cert : fwRootForHost(host);
    // B1 (see connectTo): the record buffers are taken before the DNS lookup and the socket.
    IPAddress address;
    int r = connectTo(host, address, port, ca);
    // -9984 also comes from an allocation failure inside the signature check: retry only when the heap could
    // carry a second session at all (records 2 x 16.7 KB), else it doubles time and heap at the worst moment.
    if (r == 0 && _lastError == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && _CA_cert == NULL && ca != FW_UPDATE_ROOTS_PEM
        && !_use_insecure && !fwTlsFlagsAreDate(_lastTimeFlags) && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= MC_TLS_RETRY_MIN_BLK) {
        log_w("single trust anchor did not verify %s, retrying with the full bundle", host);
        r = connectTo(host, address, port, FW_UPDATE_ROOTS_PEM);
    }
    return r;
}

int McTlsClient::connect(const char *host, uint16_t port, int32_t timeout)
{
    _timeout = timeout;
    return connect(host, port);
}

// FORMAL-01 IDF #3: WiFi.hostByName() ran dns_gethostbyname() in the calling (AU) task, but with core
// locking off the lwIP DNS table may only be touched from the tcpip thread. mcDnsGethostbyname() makes
// that call in the tcpip thread. For ERR_INPROGRESS the result arrives later from the found-callback
// (also tcpip thread); the caller waits on a binary semaphore for the 15 s hostByName() waited.
// The callback never touches the caller's stack: it writes a static slot guarded by a request id (same
// idea as AE-03 / DNS_CB_PATCHED in tools/arduino_esp32_patch.py). After a timeout the id is bumped, so
// a late callback of that request carries an old id and does nothing (no write, no semaphore give).
// Callers are serialised by a mutex, so the slot has one owner at a time. Residual window (as in AE-03):
// a callback already past its id check while the caller times out may still store its word; the next
// request clears the slot after bumping the id before it reads it.
namespace {
constexpr uint32_t MC_TLS_DNS_WAIT_MS = 15000; // WiFi.hostByName(): waitStatusBits(DNS_DONE, 15000)
constexpr uint32_t MC_TLS_DNS_LOCK_MS = 16000; // WiFi.hostByName(): waitStatusBits(DNS_IDLE, 16000)
volatile uint32_t s_tlsDnsId = 0;              // request the caller currently waits for
volatile uint32_t s_tlsDnsAddr = 0;            // its IPv4 result (0 = none)
SemaphoreHandle_t s_tlsDnsLock = nullptr;      // created once, never deleted (a late callback may still see s_tlsDnsDone)
SemaphoreHandle_t s_tlsDnsDone = nullptr;

void tlsDnsFound(const char *, const ip_addr_t *ipaddr, void *arg)
{
    if ((uint32_t)(uintptr_t)arg != s_tlsDnsId) {
        return; // stale callback of a timed-out request
    }
    s_tlsDnsAddr = (ipaddr != nullptr) ? (uint32_t)ipaddr->u_addr.ip4.addr : 0;
    xSemaphoreGive(s_tlsDnsDone);
}

// true and ip set on success; false on error or timeout (same contract as WiFi.hostByName()).
bool tlsResolve(const char *host, IPAddress &ip)
{
    if (ip.fromString(host)) {
        return true; // literal address, as hostByName() does
    }
    static portMUX_TYPE initMux = portMUX_INITIALIZER_UNLOCKED;
    if (s_tlsDnsLock == nullptr || s_tlsDnsDone == nullptr) {
        SemaphoreHandle_t lock = xSemaphoreCreateMutex();
        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        if (lock == nullptr || done == nullptr) {
            if (lock != nullptr) vSemaphoreDelete(lock);
            if (done != nullptr) vSemaphoreDelete(done);
            return false;
        }
        bool installed = false;
        portENTER_CRITICAL(&initMux);
        if (s_tlsDnsLock == nullptr && s_tlsDnsDone == nullptr) {
            s_tlsDnsLock = lock;
            s_tlsDnsDone = done;
            installed = true;
        }
        portEXIT_CRITICAL(&initMux);
        if (!installed) { // another task won the race
            vSemaphoreDelete(lock);
            vSemaphoreDelete(done);
        }
    }
    if (xSemaphoreTake(s_tlsDnsLock, pdMS_TO_TICKS(MC_TLS_DNS_LOCK_MS)) != pdTRUE) {
        return false;
    }
    s_tlsDnsAddr = 0;
    const uint32_t id = ++s_tlsDnsId;
    xSemaphoreTake(s_tlsDnsDone, 0); // drop a give left over from an earlier request
    ip_addr_t addr;
    ip_addr_set_zero(&addr);
    err_t err = mcDnsGethostbyname(host, &addr, tlsDnsFound, (void *)(uintptr_t)id);
    uint32_t result = 0;
    if (err == ERR_OK) {
        result = (uint32_t)addr.u_addr.ip4.addr;
    } else if (err == ERR_INPROGRESS) {
        if (xSemaphoreTake(s_tlsDnsDone, pdMS_TO_TICKS(MC_TLS_DNS_WAIT_MS)) == pdTRUE) {
            result = s_tlsDnsAddr;
        }
        ++s_tlsDnsId; // timeout or done: a later callback of this request is stale now
    }
    xSemaphoreGive(s_tlsDnsLock);
    if (result == 0) {
        return false;
    }
    ip = IPAddress(result);
    return true;
}
} // namespace

// host != NULL and ip unset (0.0.0.0): resolve after the buffers are allocated. ip set: use it.
int McTlsClient::connectTo(const char *host, IPAddress ip, uint16_t port, const char *ca_pem)
{
    const bool resolve = (ip == IPAddress((uint32_t)0));
    stop(); // a reused client starts from a clean context
    _lastTimeFlags = 0;

    int ret = mc_ssl_prepare(sslclient, host, _use_insecure); // B1
    if (ret == 0 && !_use_insecure) {
        ret = mc_ssl_set_ca(sslclient, ca_pem);
    }
    if (ret != 0) {
        _lastError = ret;
        log_e("mc_ssl prepare: %d (free heap %u)", ret, ESP.getFreeHeap());
        stop();
        return 0;
    }
    if (resolve && !tlsResolve(host, ip)) {
        _lastError = -1;
        stop();
        return 0;
    }
    ret = mc_ssl_connect(sslclient, ip, port, _timeout);
    _lastError = ret;
    _lastTimeFlags = sslclient->time_flags; // before stop() re-initialises the context
    if (ret < 0) {
        log_e("mc_ssl_connect: %d", ret);
        stop();
        return 0;
    }
    _connected = true;
    return 1;
}

int McTlsClient::peek()
{
    if (_peek >= 0) {
        return _peek;
    }
    _peek = timedRead();
    return _peek;
}

size_t McTlsClient::write(uint8_t data)
{
    return write(&data, 1);
}

int McTlsClient::read()
{
    uint8_t data = -1;
    int res = read(&data, 1);
    if (res < 0) {
        return res;
    }
    return data;
}

size_t McTlsClient::write(const uint8_t *buf, size_t size)
{
    if (!_connected) {
        return 0;
    }
    int res = mc_ssl_send(sslclient, buf, size);
    if (res < 0) {
        stop();
        res = 0;
    }
    return res;
}

int McTlsClient::read(uint8_t *buf, size_t size)
{
    int peeked = 0;
    int avail = available();
    if ((!buf && size) || avail <= 0) {
        return -1;
    }
    if (!size) {
        return 0;
    }
    if (_peek >= 0) {
        buf[0] = _peek;
        _peek = -1;
        size--;
        avail--;
        if (!size || !avail) {
            return 1;
        }
        buf++;
        peeked = 1;
    }

    int res = mc_ssl_receive(sslclient, buf, size);
    if (res < 0) {
        stop();
        return peeked ? peeked : res;
    }
    return res + peeked;
}

int McTlsClient::available()
{
    int peeked = (_peek >= 0);
    if (!_connected) {
        return peeked;
    }
    int res = mc_ssl_data_to_read(sslclient);
    if (res < 0) {
        stop();
        return peeked ? peeked : res;
    }
    return res + peeked;
}

uint8_t McTlsClient::connected()
{
    uint8_t dummy = 0;
    read(&dummy, 0);
    return _connected;
}

void McTlsClient::setInsecure()
{
    _CA_cert = NULL;
    _use_insecure = true;
}

void McTlsClient::setCACert(const char *rootCA)
{
    _CA_cert = rootCA;
    _use_insecure = false;
}

int McTlsClient::lastError(char *buf, const size_t size)
{
    if (!_lastError) {
        return 0;
    }
    mbedtls_strerror(_lastError, buf, size);
    return _lastError;
}

void McTlsClient::setHandshakeTimeout(unsigned long handshake_timeout)
{
    sslclient->handshake_timeout = handshake_timeout * 1000;
}

int McTlsClient::setTimeout(uint32_t seconds)
{
    _timeout = seconds * 1000;
    if (sslclient->socket >= 0) {
        struct timeval tv;
        tv.tv_sec = seconds;
        tv.tv_usec = 0;
        if (setSocketOption(SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval)) < 0) {
            return -1;
        }
        return setSocketOption(SO_SNDTIMEO, (char *)&tv, sizeof(struct timeval));
    }
    return 0;
}

int McTlsClient::fd() const
{
    return sslclient->socket;
}

#endif // ESP32

