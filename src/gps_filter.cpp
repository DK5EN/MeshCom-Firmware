/*
 * Pure altitude / plausibility helpers for the GPS path.
 * No Arduino dependencies: this file also builds on the host.
 */

#include "gps_filter.h"

#include <math.h>

void altFilterReset(struct AltFilter *f)
{
    f->init = false;
}

void altFilterSeed(struct AltFilter *f, float alt)
{
    f->x       = alt;
    f->P       = ALT_KF_P0;
    f->rejects = 0;
    f->init    = true;
}

bool altFilterUpdate(struct AltFilter *f, float meas)
{
    if (!f->init)
    {
        altFilterSeed(f, meas);
        return true;
    }

    float innov = meas - f->x;

    if (fabsf(innov) > ALT_KF_GATE_M)
    {
        f->rejects++;
        if (f->rejects >= ALT_KF_RESEED_N)
        {
            altFilterSeed(f, meas);
            return true;
        }
        return false;
    }

    f->rejects = 0;
    f->P += ALT_KF_Q;

    float K = f->P / (f->P + ALT_KF_R);
    f->x += K * innov;
    f->P *= (1.0f - K);

    return true;
}

bool altFilterConverged(const struct AltFilter *f)
{
    return f->init && f->P < ALT_KF_P_CONV;
}

bool gpsSamplePlausible(double lat, double lon, int year, int month, int day)
{
    if (lat == 0.0 || lon == 0.0)
        return false;
    if (fabs(lat) > 90.0 || fabs(lon) > 180.0)
        return false;
    if (month < 1 || month > 12)
        return false;
    if (day < 1 || day > 31)
        return false;
    if (year < 2024 || year > 2099)
        return false;

    return true;
}
