#if defined(NRF52_SERIES)

#include <Arduino.h>
#include "hw_random.h"
#include <nrf_soc.h>
#include <nrf_sdm.h>

bool mcRandomFill(uint8_t *out, size_t n)
{
    // via the SoftDevice when it is enabled (NRF_RNG is owned by it then), else the peripheral directly
    uint8_t sd_en = 0;
    sd_softdevice_is_enabled(&sd_en);
    size_t i = 0;
    uint32_t t0 = millis();
    if (sd_en)
    {
        while (i < n && (uint32_t)(millis() - t0) < 200)
        {
            uint8_t avail = 0;
            sd_rand_application_bytes_available_get(&avail);
            size_t want = n - i;
            if (avail > 0 && want > avail)
                want = avail;
            if (avail > 0 && sd_rand_application_vector_get(out + i, (uint8_t)want) == NRF_SUCCESS)
                i += want;
            else
                delay(1);
        }
    }
    else
    {
        NRF_RNG->EVENTS_VALRDY = 0;
        NRF_RNG->TASKS_START = 1;
        while (i < n)
        {
            uint32_t t1 = millis();
            while (!NRF_RNG->EVENTS_VALRDY && (uint32_t)(millis() - t1) < 50)
                ;
            if (!NRF_RNG->EVENTS_VALRDY)
                break;
            out[i++] = (uint8_t)NRF_RNG->VALUE;
            NRF_RNG->EVENTS_VALRDY = 0;
        }
        NRF_RNG->TASKS_STOP = 1;
    }
    // the hardware RNG did not answer in time (should never happen): no nonce, no session
    return i == n;
}

#endif
