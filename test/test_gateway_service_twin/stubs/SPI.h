// Empty shim: src/nrf52/gateway_service_nrf52.cpp includes <SPI.h> because it
// was carved out of nrf52_main.cpp, which needed it for the W5100S driver.
// gatewayService_nrf52() itself uses nothing from it. Copied from the U1/U2
// twins (test/test_udp_send_twin/stubs/SPI.h), same reasoning.
#pragma once
