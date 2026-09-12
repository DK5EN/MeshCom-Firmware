// Empty shim: src/nrf52/udp_frame_nrf52.cpp includes <SPI.h> because it was
// carved out of nrf_eth.cpp, which needed it for the W5100S driver. The
// frame handler itself uses nothing from it. Copied from the U2 twin
// (test/test_udp_send_twin/stubs/SPI.h), same reasoning.
#pragma once
