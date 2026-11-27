#pragma once

#include <stddef.h>
#include <stdint.h>

// Hardware random bytes (WebAuthRng signature). true when all n bytes were filled.
// ESP32: esp_fill_random (src/esp32/hw_random_esp32.cpp), never fails.
// nRF52: SoftDevice RNG when enabled (200 ms bound), else NRF_RNG directly (50 ms per
// byte); false when the hardware did not answer in time (src/nrf52/hw_random_nrf52.cpp).
bool mcRandomFill(uint8_t *out, size_t n);
