// FORMAL-02 wave 8 (B8): spectrum sweep glue. One blocking run on "Scan now", result kept for the page.
#include "spectral_sweep.h"

#include <Arduino.h>
#include <new>
#include "configuration.h"

#if defined(ESP32) && !defined(BOARD_T_DECK_PRO) && \
    (defined(SX1262X) || defined(SX126X) || defined(SX1268_V3) || defined(SX1262_V3) || defined(SX1262_E290) || defined(USING_SX1262))

#include "spectral_scan.h"
#include "lora_setchip.h"
#include "watchdog_feed.h"
#include "esp32/esp32_main.h"
#include "meshcom_settings.h"
#include "loop_functions_extern.h"   // bRadio


static SpecSweepResult *s_result = nullptr;   // allocated at the first run, kept (never freed)
static bool s_have = false;
static int8_t s_last_rc = SPS_OK;

// Chip back to LoRa as setup left it, then RX armed. false: a step failed (both log their own line).
static bool spectralSweepRestore(void)
{
    const bool chipOk = (sx126x_spectral_finish_scan() == RADIOLIB_ERR_NONE);
    const bool rxOk = loraRxRearmAfterScan();
    return chipOk && rxOk;
}

static int8_t spectralSweepRunOnce(void)
{
    if (!bRadio)
        return SPS_NORADIO;

    if (!loraRxDisarmForScan())
        return SPS_BUSY;

    if (s_result == nullptr)
    {
        s_result = new (std::nothrow) SpecSweepResult;
        if (s_result == nullptr)
        {
            // the chip was not touched yet: only RX needs re-arming
            return loraRxRearmAfterScan() ? SPS_NOMEM : SPS_REINIT;
        }
    }

    const SpecSweepRange r = specSweepRangeFor(getFreq());

    if (sx126x_spectral_init_scan(r.start) != RADIOLIB_ERR_NONE)
    {
        return spectralSweepRestore() ? SPS_INIT : SPS_REINIT;   // a deaf radio outranks the init failure
    }

    // Filled in place (no second 1.4 KB buffer: static RAM is tight on the classic ESP32); the previous
    // result is gone once a run starts, a failed run shows "No scan yet".
    s_have = false;
    uint8_t *const tmp = s_result->packed;
    memset(tmp, 0, sizeof s_result->packed);
    const uint8_t steps = specSweepStepCount(r);
    int8_t rc = SPS_OK;
    float cur = r.start;
    for (uint8_t s = 0; s < steps; s++)
    {
        uint16_t *res = sx126x_spectral_scan_freq(cur, (uint16_t)r.samples);
        if (res == nullptr)
        {
            rc = SPS_TIMEOUT;
            break;
        }
        for (uint8_t i = 0; i < SPS_BINS; i++)
            specSweepSetNibble(tmp + (size_t)s * SPS_BYTES_PER_STEP, i, specSweepLevel(res[i], r.samples));
        delete[] res;

        yield();
        meshcom_wdt_feed();
        cur += r.step;
    }

    const bool restored = spectralSweepRestore();

    if (rc == SPS_OK)
    {
        s_result->start = r.start;
        s_result->end = r.end;
        s_result->step = r.step;
        s_result->samples = r.samples;
        s_result->steps = steps;
        s_result->own_freq = meshcom_settings.node_freq;
        s_result->own_bw_khz = meshcom_settings.node_bw;
        s_result->scanned_at_ms = millis();
        s_have = true;
    }

    return restored ? rc : (int8_t)SPS_REINIT;   // a deaf radio outranks the sweep result
}

int8_t spectralSweepRun(void)
{
    s_last_rc = spectralSweepRunOnce();
    return s_last_rc;
}

bool spectralSweepSupported(void)
{
    return true;
}

int8_t spectralSweepLastRc(void)
{
    return s_last_rc;
}

static void emitToPrint(void *ctx, const char *text)
{
    static_cast<Print *>(ctx)->print(text);
}

void spectralSweepRender(Print &out)
{
    if (!s_have || s_result == nullptr)
    {
        out.println("<p>No scan yet. Press Scan now.</p>");
        return;
    }
    specSweepRenderText(emitToPrint, &out, *s_result);
}

#else

int8_t spectralSweepRun(void)
{
    return SPS_UNSUPPORTED;
}

bool spectralSweepSupported(void)
{
    return false;
}

int8_t spectralSweepLastRc(void)
{
    return SPS_OK;
}

void spectralSweepRender(Print &out)
{
    out.println("<p>Spectrum scan is not supported on this device.</p>");
}

#endif
