#pragma once

// C4 carve-out (DRY unification, audit rows D1-09 / D1-10): the gateway
// service block out of the two loop functions, one file per platform.
//
// Unifying these is explicitly out of scope -- the audit rates D1-09 at risk 3
// and puts it behind a soak, last in the wave order. The carve is done anyway
// because it costs nothing and makes the drift measurable for the matrix
// instead of leaving it buried 2000 lines into esp32loop()/nrf52loop().
//
// Both carry the same shape: a bGATEWAY branch that services the UDP socket
// and watches the server heartbeat, and an else-branch that only harvests the
// NTP reply (TM-45), so exactly one socket read happens per loop pass. What
// they do inside is where the drift is, and it is large:
//
//   - ESP32 has a two-stage heartbeat watchdog (warn at HB_WARN_TIME, act at
//     MAX_HB_RX_TIME) and decides on WiFi.status(); nRF52 has one stage and
//     decides on neth.hasIPaddress.
//   - nRF52 sends only when getUDP() reported "no packet received"; ESP32
//     always calls getMeshComUDP() then sendMeshComUDP().
//   - nRF52 guards the SPI bus around the Ethernet access (bSPI_ETH_Active)
//     and replays a deferred radio RX afterwards; ESP32 has no such guard.
//   - the recovery paths differ entirely: fixed IP vs DHCP retry on nRF52
//     (and N-20: resetDHCP() rather than a full PHY reset), resetMeshComUDP()
//     on ESP32.
//
// Those become drift-matrix rows DR-14..DR-16. Nothing here changes behaviour.
//
// D1-10, the loop scheduler, is NOT carved: it is ~18 timer predicates spread
// through a 2162-line and a 1437-line loop, and extracting them is a much
// larger and riskier change than this one. Still owed.
void gatewayService_esp32(void);
void gatewayService_nrf52(void);
