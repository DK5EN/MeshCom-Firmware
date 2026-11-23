// FORMAL-01 lwIP #3: see mc_dns.h.
#if defined(ESP32) && !defined(NATIVE_BUILD)
#include "mc_dns.h"
#include "lwip/tcpip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
struct DnsCall
{
    const char *host;
    ip_addr_t *addr;
    dns_found_callback cb;
    void *arg;
    err_t err;
    SemaphoreHandle_t done;
};

void dnsOnTcpip(void *p)
{
    DnsCall *c = static_cast<DnsCall *>(p);
    c->err = dns_gethostbyname(c->host, c->addr, c->cb, c->arg);
    xSemaphoreGive(c->done); // last access to *c: the caller frees it after the take
}
} // namespace

err_t mcDnsGethostbyname(const char *host, ip_addr_t *addr, dns_found_callback cb, void *arg)
{
    DnsCall c;
    c.host = host;
    c.addr = addr;
    c.cb = cb;
    c.arg = arg;
    c.err = ERR_MEM;
    c.done = xSemaphoreCreateBinary();
    if (c.done == nullptr)
        return ERR_MEM;
    // tcpip_try_callback: never blocks the loop on a full tcpip mailbox (tcpip_callback would wait forever)
    if (tcpip_try_callback(dnsOnTcpip, &c) != ERR_OK)
    {
        vSemaphoreDelete(c.done);
        return ERR_MEM;
    }
    // Once queued the callback always runs, so wait without timeout: c, host and addr stay valid.
    xSemaphoreTake(c.done, portMAX_DELAY);
    vSemaphoreDelete(c.done);
    return c.err;
}
#endif
