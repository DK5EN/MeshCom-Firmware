#ifndef _ALT_FUSION_H_
#define _ALT_FUSION_H_

/*
 * GPS/barometer complementary altitude filter (GPS-05b).
 * No Arduino dependencies: this file also builds on the host.
 *
 * out = LPF_tau(gps) + (baro - LPF_tau(baro))
 *
 * A single-pole, irregular-sample complementary filter. The GPS side is
 * low-passed to reject its noise; the barometer side is high-passed (its
 * own low-pass subtracted back out) to reject its drift while keeping its
 * short-term precision. Because only the barometer's HIGH-pass component
 * (baro - LPF(baro)) is added to the output, any constant offset in the
 * barometric channel cancels exactly: getPressALTf() anchors on whatever
 * pressure/altitude pair happened to latch at filter convergence
 * (docs/bug-baro-altitude-20260906.md S5.2), and that arbitrary anchor never
 * reaches the fused output, only the pressure's short-term wiggle does.
 *
 * gpsAlt is meant to be the already-gated GPS estimate from gps_filter.h
 * (struct AltFilter's x), not the raw NMEA altitude: the Kalman gate has
 * already removed single-sample outliers, so the low-pass here only has to
 * smooth the remaining multi-minute wander (measured on the DK5EN-93
 * corpus: Kalman input sd 2.76 m fused, raw input 3.17 m).
 *
 * ALT_FUSION_TAU_MS = 30 min: the knee of the tau sweep in
 * docs/bug-baro-altitude-20260906.md S7 -- below it GPS wander leaks
 * through, above it the filter starts tracking the barometer's own weather
 * drift over the same timescale and 30-minute stability gets worse again.
 */

#include <stdbool.h>
#include <stdint.h>

#define ALT_FUSION_TAU_MS 1800000u /* 30 min: knee of the tau sweep, bug doc S7 */

struct AltFusion
{
    float gpsLp;  /* low-passed GPS altitude estimate (m) */
    float baroLp; /* low-passed barometric altitude (m) */
    bool  init;   /* seeded */
};

/* init = false; nothing else is touched */
void altFusionReset(struct AltFusion *f);

/*
 * One update. dtMs is the wall time since the previous update (0 on the
 * first call after altFusionReset() is fine: the first call always seeds).
 *
 * On the first call (f->init == false), both low-passes seed exactly on
 * their inputs and the function returns gpsAlt unchanged -- enabling the
 * fusion, or reseeding it (e.g. on TRACK-mode entry, together with the GPS
 * Kalman filter), never steps the output.
 *
 * Otherwise a = 1 - exp(-dtMs / ALT_FUSION_TAU_MS) is the per-update blend
 * factor; both low-passes are updated toward their current sample by that
 * factor, and the return value is gpsLp + (baroAlt - baroLp). dtMs == 0
 * yields a == 0 (both low-passes and the output are unchanged). No dt is
 * clamped: an unusually long gap converges both low-passes onto the
 * current samples, which is the correct behaviour for a low-pass filter
 * fed at an irregular cadence.
 */
float altFusionUpdate(struct AltFusion *f, float gpsAlt, float baroAlt, uint32_t dtMs);

#endif /* _ALT_FUSION_H_ */
