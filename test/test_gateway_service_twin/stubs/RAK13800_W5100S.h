// Empty shim: see SPI.h. The W5100S driver reaches gatewayService_nrf52()
// only through NrfETH, which is stubbed in nrf_eth.h. Copied from the U1/U2
// twins (test/test_udp_send_twin/stubs/RAK13800_W5100S.h), same reasoning.
#pragma once
