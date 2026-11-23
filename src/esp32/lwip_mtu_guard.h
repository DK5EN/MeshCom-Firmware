#pragma once

// FORMAL-01 lwIP #2: pure decision of the link-time MTU guard (lwip_mtu_guard.cpp).
// lwIP (IDF 4.4) accepts DHCP option 26 values 20..27; ip4_frag then never ends.
// No Arduino/lwIP includes: host-testable.

#include <stdint.h>

namespace mc_lwip_guard {

// RFC 791: every host must accept datagrams of 576 bytes. Below this a netif->mtu
// is never legitimate here (netif_mtu::MTU_MIN = 1280 is the stricter configured clamp).
constexpr uint16_t MTU_FLOOR = 576;

}  // namespace mc_lwip_guard

// 0 means "no MTU limit" in lwIP (ip4 skips fragmentation, TCP uses its own MSS): keep it.
// 1..575 is raised to MTU_FLOOR; everything else passes unchanged.
inline uint16_t mcMtuFloor(uint16_t mtu)
{
    return (mtu != 0 && mtu < mc_lwip_guard::MTU_FLOOR) ? mc_lwip_guard::MTU_FLOOR : mtu;
}
