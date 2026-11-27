#if defined(ESP32)

#include "hw_random.h"
#include <esp_random.h>

bool mcRandomFill(uint8_t *out, size_t n)
{
    esp_fill_random(out, n); // hardware TRNG
    return true;
}

#endif
