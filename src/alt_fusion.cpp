/*
 * GPS/barometer complementary altitude filter (GPS-05b).
 * No Arduino dependencies: this file also builds on the host.
 */

#include "alt_fusion.h"

#include <math.h>

void altFusionReset(struct AltFusion *f)
{
    f->init = false;
}

float altFusionUpdate(struct AltFusion *f, float gpsAlt, float baroAlt, uint32_t dtMs)
{
    if (!f->init)
    {
        f->gpsLp  = gpsAlt;
        f->baroLp = baroAlt;
        f->init   = true;
        return gpsAlt;
    }

    float a = 1.0f - expf(-(float)dtMs / (float)ALT_FUSION_TAU_MS);

    f->gpsLp  += a * (gpsAlt - f->gpsLp);
    f->baroLp += a * (baroAlt - f->baroLp);

    return f->gpsLp + (baroAlt - f->baroLp);
}
