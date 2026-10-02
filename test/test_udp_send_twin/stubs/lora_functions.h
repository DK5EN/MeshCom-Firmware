// Empty shim: nrf52/udp_drain_nrf52.cpp and esp32/udp_drain_esp32.cpp both
// include <lora_functions.h> as inherited from their origin TUs. The drain
// calls nothing from it -- decodeAPRS() comes from aprs_functions.h, which
// the real lora_functions.h happens to re-include.
#pragma once
#include <aprs_functions.h>
