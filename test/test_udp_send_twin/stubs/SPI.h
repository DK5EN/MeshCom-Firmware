// Empty shim: nrf52/udp_drain_nrf52.cpp includes <SPI.h> because it was
// carved out of nrf52_main.cpp, which needed it. The drain itself uses
// nothing from it.
#pragma once
