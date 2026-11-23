// FORMAL-01 lwIP #2: link-time guard, active through
//   -Wl,--wrap=netif_set_addr -Wl,--wrap=ip4_frag -Wl,--wrap=tcp_eff_send_mss_netif
// netif_set_addr: dhcp_handle_ack stores option 26 into netif->mtu, then dhcp_recv -> dhcp_bind
// (dhcp.c.obj) calls netif_set_addr (netif.c.obj) on every ACK, also a renew with the same IP.
// That closes the tcp_connect gap at the source; the other two wraps are defense in depth.
// A rogue DHCP server can set netif->mtu to 20..27 (option 26); the prebuilt liblwip
// ip4_frag then loops forever (WDT reset at every renew). Raise a too small mtu
// before the real function runs. Callers are in other objects than the definitions
// (ip4.c.obj -> ip4_frag.c.obj; tcp_in/tcp_out.c.obj -> tcp.c.obj), so --wrap sees them.

#if defined(ESP32) && !defined(NATIVE_BUILD)

#include "lwip_mtu_guard.h"

#include <lwip/err.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>

extern "C" {

static inline void mcGuardNetif(struct netif *n)
{
    if (n) {
        uint16_t f = mcMtuFloor(n->mtu);
        if (f != n->mtu)
            n->mtu = f;
    }
}

void __real_netif_set_addr(struct netif *netif, const ip4_addr_t *ipaddr, const ip4_addr_t *netmask,
                          const ip4_addr_t *gw);
void __wrap_netif_set_addr(struct netif *netif, const ip4_addr_t *ipaddr, const ip4_addr_t *netmask,
                           const ip4_addr_t *gw)
{
    mcGuardNetif(netif);
    __real_netif_set_addr(netif, ipaddr, netmask, gw);
}

err_t __real_ip4_frag(struct pbuf *p, struct netif *netif, const ip4_addr_t *dest);
err_t __wrap_ip4_frag(struct pbuf *p, struct netif *netif, const ip4_addr_t *dest)
{
    mcGuardNetif(netif);
    return __real_ip4_frag(p, netif, dest);
}

u16_t __real_tcp_eff_send_mss_netif(u16_t sendmss, struct netif *outif, const ip_addr_t *dest);
u16_t __wrap_tcp_eff_send_mss_netif(u16_t sendmss, struct netif *outif, const ip_addr_t *dest)
{
    mcGuardNetif(outif);
    return __real_tcp_eff_send_mss_netif(sendmss, outif, dest);
}

}  // extern "C"

#endif
