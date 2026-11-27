// Empty shim for the frozen nRF52 handler (frozen/udp_frame_nrf52.frozen.cpp includes <SPI.h> for the
// W5100S driver it never calls). The live handler does not need it any more.
#pragma once
