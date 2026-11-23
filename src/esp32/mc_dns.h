// FORMAL-01 lwIP #3: dns_gethostbyname() from a non-tcpip task.
// CONFIG_LWIP_TCPIP_CORE_LOCKING is off in arduino-esp32 2.0.17, so the lwIP DNS table may only
// be touched from the tcpip thread. mcDnsGethostbyname() runs dns_gethostbyname() there and waits
// for the result; the found-callback runs in the tcpip thread as before.
#pragma once

#if defined(ESP32) && !defined(NATIVE_BUILD)
#include "lwip/dns.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"

// Same contract as dns_gethostbyname(): ERR_OK (addr filled), ERR_INPROGRESS (cb later), or an error.
// ERR_MEM when the tcpip mailbox is full or no message/semaphore can be allocated (tcpip_try_callback, no blocking). host and addr must stay valid until return (they do:
// the call is synchronous). Never call from the tcpip thread itself.
err_t mcDnsGethostbyname(const char *host, ip_addr_t *addr, dns_found_callback cb, void *arg);
#endif
