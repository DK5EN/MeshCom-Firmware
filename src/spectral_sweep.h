#ifndef _SPECTRAL_SWEEP_H_
#define _SPECTRAL_SWEEP_H_

// FORMAL-02 wave 8 (B8): the spectrum sweep as ONE blocking run started by the guarded "Scan now"
// action; the Spectrum page only renders the stored last result. The pure section below (range,
// step count, level/alpha, nibble packing, SVG text) has no Arduino dependency and is tested
// natively (test/test_spectral_sweep). The glue (spectral_sweep.cpp) is SX126x boards only.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

class Print;

enum SpecSweepRc : int8_t {
    SPS_OK = 0,
    SPS_UNSUPPORTED = -1,  // board without the SX126x scan patch (T-Deck Pro, nRF52, other radios)
    SPS_BUSY = -2,         // a TX or RX is in flight, nothing was touched
    SPS_INIT = -3,         // FSK init / patch upload failed
    SPS_TIMEOUT = -4,      // a step did not finish in time (or no memory per step)
    SPS_NOMEM = -5,        // result store not allocatable
    SPS_REINIT = -6,       // the radio could not be restored after the scan (begin/extras/setchip/rx failed)
    SPS_NORADIO = -7       // no local radio came up (bRadio false)
};

#define SPS_BINS      33   // RADIOLIB_SX126X_SPECTRAL_SCAN_RES_SIZE
#define SPS_MAX_STEPS 81
#define SPS_BYTES_PER_STEP ((SPS_BINS + 1) / 2)

struct SpecSweepRange {
    float start;
    float end;
    float step;
    int samples;
};

// Same ranges the page used: 863-870/0.100 above 860 MHz, else 432-434/0.025; 2048 samples.
static inline SpecSweepRange specSweepRangeFor(float freq_mhz)
{
    SpecSweepRange r = {432.0f, 434.0f, 0.025f, 2048};
    if (freq_mhz > 860)
    {
        r.start = 863.0f;
        r.end = 870.0f;
        r.step = 0.100f;
    }
    return r;
}

// Number of scanned frequencies: the very float loop of the old page (while (cur < end) cur += step).
static inline uint8_t specSweepStepCount(const SpecSweepRange &r)
{
    unsigned n = 0;
    float cur = r.start;
    while (cur < r.end && n < SPS_MAX_STEPS)
    {
        n++;
        cur += r.step;
    }
    return (uint8_t)n;
}

// 0 = nothing seen, else 1..15 proportional to res/samples.
static inline uint8_t specSweepLevel(uint16_t res, int samples)
{
    if (res == 0 || samples <= 0)
        return 0;
    long l = lroundf(15.0f * (float)res / (float)samples);
    if (l < 1)
        l = 1;
    if (l > 15)
        l = 15;
    return (uint8_t)l;
}

// Transparency of a bin, minimum 0.2 so faint bins stay visible. The old page divided two
// integers here (always 0, so always 0.2): shading was lost.
static inline float specSweepAlpha(uint8_t level)
{
    float a = (float)level / 15.0f;
    return a < 0.2f ? 0.2f : a;
}

static inline void specSweepSetNibble(uint8_t *row, uint8_t bin, uint8_t level)
{
    uint8_t &b = row[bin / 2];
    if (bin & 1)
        b = (uint8_t)((b & 0x0F) | ((level & 0x0F) << 4));
    else
        b = (uint8_t)((b & 0xF0) | (level & 0x0F));
}

static inline uint8_t specSweepGetNibble(const uint8_t *row, uint8_t bin)
{
    return (bin & 1) ? (uint8_t)(row[bin / 2] >> 4) : (uint8_t)(row[bin / 2] & 0x0F);
}

struct SpecSweepResult {
    float start, end, step;
    int samples;
    uint8_t steps;
    float own_freq;
    float own_bw_khz;
    uint32_t scanned_at_ms;
    uint8_t packed[SPS_MAX_STEPS * SPS_BYTES_PER_STEP];
};

typedef void (*SpecSweepEmit)(void *ctx, const char *text);

// The SVG plus the two explanatory paragraphs, same element order, attributes and number
// formats as the old page. Every line goes to emit() as one string.
static inline void specSweepRenderText(SpecSweepEmit emit, void *ctx, const SpecSweepResult &r)
{
    char b[400];
    const uint16_t pw = 10, ph = 10;
    const float spec_step = r.step;
    const uint16_t num_fsteps = (uint16_t)roundf((r.end - r.start) / spec_step);
    const uint16_t start_x = 60, start_y = 10;
    const uint16_t end_x = start_x + ((num_fsteps + 1) * pw);
    const uint16_t end_y = start_y + (SPS_BINS * ph);
    const uint16_t mw = (uint16_t)round(((r.own_bw_khz / 1000) / spec_step) * pw);
    const uint16_t mc = start_x + (((r.own_freq - r.start) / spec_step) * pw);
    const uint16_t ms = mc - (mw / 2);

    snprintf(b, sizeof b, "<svg viewbox=\"0, 0, %d, %d\" id=\"spectrum_display\" style=\"font-size:var(--fs-s);\">", end_x + 40, end_y + 60);
    emit(ctx, b);
    snprintf(b, sizeof b, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" style=\"stroke:black;stroke-width:1\"/>\n", start_x, end_y, end_x, end_y);
    emit(ctx, b);
    snprintf(b, sizeof b, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" style=\"stroke:black;stroke-width:1\"/>\n", start_x, start_y, start_x, end_y);
    emit(ctx, b);
    snprintf(b, sizeof b, "<text x=\"%d\" y=\"%d\" style=\"color: black;\">Freq [MHz]</text>\n", start_x, end_y + 40);
    emit(ctx, b);
    snprintf(b, sizeof b, "<text x=\"0\" y=\"0\" f style=\"color: black;\" transform=\"translate(10, %d) rotate(-90)\")>RSSI [dBm]</text>\n", end_y);
    emit(ctx, b);
    snprintf(b, sizeof b, "<rect width=\"%d\" height=\"%d\" x=\"%d\" y=\"%d\" fill=\"rgba(0, 110, 129, 0.2)\" />", mw, end_y - start_y, ms, start_y);
    emit(ctx, b);
    snprintf(b, sizeof b, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" style=\"stroke:rgba(114, 0, 129, 0.2); stroke-width:1\"/>\n", mc, start_y, mc, end_y);
    emit(ctx, b);

    for (uint8_t i = 0; i < SPS_BINS; i++)
    {
        if (i % 3 == 0)
        {
            snprintf(b, sizeof b, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" style=\"stroke:grey; stroke-width:1\"/>\n", start_x - 10, start_y + (i * ph), end_x, start_y + (i * ph));
            emit(ctx, b);
            snprintf(b, sizeof b, "<text x=\"%d\" y=\"%d\" style=\"color: black;\">-%d</text>\n", start_x - 40, start_y + (i * ph) + 6, 11 + (i * 4));
            emit(ctx, b);
        }
    }

    float cur = r.start;
    for (uint16_t s = 0; s < r.steps; s++)
    {
        const uint8_t *row = r.packed + (size_t)s * SPS_BYTES_PER_STEP;
        for (uint8_t i = 0; i < SPS_BINS; i++)
        {
            uint8_t lv = specSweepGetNibble(row, i);
            if (lv > 0)
            {
                snprintf(b, sizeof b, "<rect width=\"%d\" height=\"%d\" x=\"%d\" y=\"%d\" fill=\"rgba(0,0,0,%0.3f)\"/>", pw, ph, start_x + (s * pw), start_y + (i * ph), (double)specSweepAlpha(lv));
                emit(ctx, b);
            }
        }
        if (s % 5 == 0)
        {
            snprintf(b, sizeof b, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" style=\"stroke:grey; stroke-width:1\"/>\n", start_x + (s * pw), end_y, start_x + (s * pw), end_y + 10);
            emit(ctx, b);
        }
        if (s % 10 == 0)
        {
            snprintf(b, sizeof b, "<text x=\"%d\" y=\"%d\" style=\"color: black;\">%.3f</text>\n", start_x + (s * pw) - 10, end_y + 25, (double)cur);
            emit(ctx, b);
        }
        cur += r.step;
    }
    emit(ctx, "</svg>\r\n");  // println() of the old page: CRLF

    snprintf(b, sizeof b, "<p>The Node scans the frequency range (%.3fMHz - %.3fMHz) in small steps using a given scan bandwith (%.3fMHz).</p>\n", (double)r.start, (double)r.end, (double)r.step);
    emit(ctx, b);
    snprintf(b, sizeof b, "<p>It performs %i measurements each step and puts them into %i power-bins. That is why you will see several measurement-dots for each step. The darker the dot, the more measurements have been put into that certain power bin.</p>", r.samples, SPS_BINS);
    emit(ctx, b);
}

// Glue (spectral_sweep.cpp).
// Returns SPS_*; on SPS_OK the new result is stored. The RX path is re-armed on every path that
// touched the radio. "Scan now" must be a guarded (non-GET-from-link) action; the page itself only renders.
int8_t spectralSweepRun(void);
// Writes the stored result (SVG + two paragraphs) or one "No scan yet" paragraph. No subheader,
// no content div, no button.
void spectralSweepRender(Print &out);

// true on the boards whose radio runs the sweep (the page shows the "Scan now" button only there)
bool spectralSweepSupported(void);
// the code of the last spectralSweepRun() since boot; SPS_OK before the first run
int8_t spectralSweepLastRc(void);

#endif
