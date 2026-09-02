#ifndef _GPS_FILTER_H_
#define _GPS_FILTER_H_

/*
 * Pure altitude / plausibility helpers for the GPS path.
 * No Arduino dependencies: this file also builds on the host.
 */

#include <stdbool.h>
#include <stdint.h>

/* Scalar Kalman filter on the GPS altitude, constant measurement noise. */
#define ALT_KF_Q        0.01f   /* process noise per update, tau ~ 410 s at 3 s cadence */
#define ALT_KF_R        185.0f  /* measurement noise (m^2), (4 m * 1.7 * 2.0)^2          */
#define ALT_KF_P0       400.0f  /* initial covariance after a seed                        */
#define ALT_KF_GATE_M   15.0f   /* innovation gate: larger jumps are rejected             */
#define ALT_KF_RESEED_N 10      /* consecutive rejects that re-seed the filter            */
#define ALT_KF_P_CONV   2.5f    /* P below this value = converged                          */

struct AltFilter
{
    float   x;        /* altitude estimate (m) */
    float   P;        /* estimate covariance (m^2) */
    uint8_t rejects;  /* consecutive gate rejections */
    bool    init;     /* seeded */
};

/* init = false; nothing else is touched */
void altFilterReset(struct AltFilter *f);
/* x = alt, P = ALT_KF_P0, rejects = 0, init = true */
void altFilterSeed(struct AltFilter *f, float alt);
/* one measurement; returns false when the sample was rejected by the gate */
bool altFilterUpdate(struct AltFilter *f, float meas);
/* init && P < ALT_KF_P_CONV */
bool altFilterConverged(const struct AltFilter *f);

/*
 * Plausibility of a decoded fix. Rejects null island (lat or lon exactly 0.0),
 * angles out of range, and calendar values the parser cannot have produced.
 */
bool gpsSamplePlausible(double lat, double lon, int year, int month, int day);

#endif /* _GPS_FILTER_H_ */
