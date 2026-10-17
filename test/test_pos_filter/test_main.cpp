// Native test suite for the horizontal position Kalman filter in
// src/pos_filter.h / pos_filter.cpp (GPS-11): cases 1-12 of the spec plus the
// W1 rework cases (FIXED long-horizon average, minimum gate distance, mode
// switch, reject timer, correlated multipath noise) and the FIXED motion
// detection (rework 3: walking with `--track off`, false triggers, return to
// still, one outlier after a long gap).
//
// All noise comes from a seeded xorshift32 + Box-Muller generator (no rand()),
// so every run is deterministic.
//
//   pio test -e native -f test_pos_filter

#include <unity.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <pos_filter.h>

void setUp(void)    {}
void tearDown(void) {}

#define PI_D     3.14159265358979323846
#define M_PER_DEG (6371000.0 * PI_D / 180.0)

// Anchor of the synthetic trajectories (mid-latitude north-east).
#define A_LAT 48.0
#define A_LON 11.0

// ---------------------------------------------------------------------------
// PRNG
// ---------------------------------------------------------------------------

struct Rng { uint32_t s; };

static void rngInit(struct Rng *r, uint32_t seed) { r->s = seed ? seed : 1u; }

static double rngUniform(struct Rng *r)
{
    r->s ^= r->s << 13;
    r->s ^= r->s >> 17;
    r->s ^= r->s << 5;
    return ((double)(r->s >> 8) + 0.5) / 16777216.0;   // (0,1)
}

static double rngGauss(struct Rng *r, double sd)
{
    double u1 = rngUniform(r);
    double u2 = rngUniform(r);
    return sd * sqrt(-2.0 * log(u1)) * cos(2.0 * PI_D * u2);
}

// ---------------------------------------------------------------------------
// Frame helpers: metres east/north of (A_LAT, A_LON) <-> degrees
// ---------------------------------------------------------------------------

static double cosA(void) { return cos(A_LAT * PI_D / 180.0); }
static double latOf(double y) { return A_LAT + y / M_PER_DEG; }
static double lonOf(double x) { return A_LON + x / (M_PER_DEG * cosA()); }
static double eastOf(double lon) { return (lon - A_LON) * M_PER_DEG * cosA(); }
static double northOf(double lat) { return (lat - A_LAT) * M_PER_DEG; }

// filter output in metres relative to the anchor frame
static void outM(const struct PosFilter *f, double *x, double *y)
{
    double lat, lon;
    posFilterGet(f, &lat, &lon);
    *x = eastOf(lon);
    *y = northOf(lat);
}

static const uint32_t DT1 = 1000u;
static const uint32_t DT3 = 3000u;

// one update from metres
static enum PosFilterResult upd(struct PosFilter *f, double x, double y, float hdop,
                                bool vv, float spd, float crs, enum PosFilterMode m, uint32_t dt)
{
    return posFilterUpdate(f, latOf(y), lonOf(x), hdop, vv, spd, crs, m, dt);
}

// ---------------------------------------------------------------------------
// 1. Seed
// ---------------------------------------------------------------------------

void test_seed_liefert_erste_probe_unveraendert(void)
{
    struct PosFilter f;
    posFilterReset(&f);
    TEST_ASSERT_FALSE(f.init);

    double lat = 48.123456, lon = 11.654321;
    TEST_ASSERT_EQUAL_INT(POS_KF_RESEED, posFilterUpdate(&f, lat, lon, 1.5f, false, 0, 0, POS_KF_MODE_FIXED, 0));
    TEST_ASSERT_TRUE(f.init);

    double glat, glon;
    posFilterGet(&f, &glat, &glon);
    TEST_ASSERT_TRUE(lat == glat);
    TEST_ASSERT_TRUE(lon == glon);
    TEST_ASSERT_TRUE(posFilterSigmaM(&f) > 0.0f);

    // explicit seed with a valid velocity
    posFilterSeed(&f, lat, lon, 1.5f, true, 10.0f, 90.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, f.e.v);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, f.n.v);
}

// the seed takes the HDOP: sigma = sqrt(2) * UERE * max(hdop, floor) / sqrt(2)
void test_seed_mit_hdop(void)
{
    struct PosFilter f;
    posFilterSeed(&f, 48.0, 11.0, 2.0f, false, 0, 0);
    TEST_ASSERT_TRUE(f.init);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 8.0f, posFilterSigmaM(&f));
    posFilterSeed(&f, 48.0, 11.0, 0.3f, false, 0, 0);          // floored to 0.8
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.2f, posFilterSigmaM(&f));
    posFilterSeed(&f, 48.0, 11.0, NAN, false, 0, 0);           // invalid -> floor, finite
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.2f, posFilterSigmaM(&f));
}

// ---------------------------------------------------------------------------
// 2. Stationary noise: sigma 8 m white + slow random walk (3 m after 1 h), FIXED
// ---------------------------------------------------------------------------

static void statRun(double *rawRms, double *filtRms)
{
    struct Rng r; rngInit(&r, 0x2545F491u);
    struct PosFilter f; posFilterReset(&f);
    double wx = 0, wy = 0, sr = 0, sf = 0;
    int n = 0;

    for (int t = 0; t < 3600; t++)
    {
        // random walk, 3 m std after 3600 steps
        wx += rngGauss(&r, 3.0 / 60.0);
        wy += rngGauss(&r, 3.0 / 60.0);
        double mx = wx + rngGauss(&r, 8.0);
        double my = wy + rngGauss(&r, 8.0);
        // a resting receiver reports speed ~0 with a random course (zero-velocity update)
        upd(&f, mx, my, 2.8f, true, 0.05f, (float)(rngUniform(&r) * 360.0), POS_KF_MODE_FIXED, DT1);
        if (t >= 300)
        {
            double fx, fy;
            outM(&f, &fx, &fy);
            sr += mx * mx + my * my;
            sf += fx * fx + fy * fy;
            n++;
        }
    }
    *rawRms  = sqrt(sr / n);
    *filtRms = sqrt(sf / n);
}

void test_stationaer_rauschen_viertel(void)
{
    double raw, filt;
    statRun(&raw, &filt);
    printf("stationary raw=%.2f filt=%.2f ratio=%.3f\n", raw, filt, filt / raw);
    TEST_ASSERT_TRUE_MESSAGE(raw > 10.0, "raw rms must reflect sigma 8 m per axis");
    TEST_ASSERT_TRUE_MESSAGE(filt <= 0.25 * raw, "filtered spread must be <= 1/4 of raw");
}

// ---------------------------------------------------------------------------
// 3. HDOP weighting
// ---------------------------------------------------------------------------

// converge on the origin, then a single east offset at the given HDOP
static double outlierMove(enum PosFilterMode m, float hdop, double off, enum PosFilterResult *res)
{
    struct PosFilter f; posFilterReset(&f);
    for (int i = 0; i < 40; i++)
        upd(&f, 0, 0, 2.0f, true, 0.0f, 0, m, DT1);
    *res = upd(&f, off, 0, hdop, true, 0.0f, 0, m, DT1);
    printf("outlier %.0f m hdop %.0f %s: res=%d Ppp=%.3f\n", off, (double)hdop,
           m == POS_KF_MODE_FIXED ? "FIXED" : "TRACK", (int)*res, f.e.Ppp);
    double x, y;
    outM(&f, &x, &y);
    return fabs(x);
}

void test_hdop_gewichtung(void)
{
    enum PosFilterMode modes[2] = { POS_KF_MODE_TRACK, POS_KF_MODE_FIXED };
    for (int k = 0; k < 2; k++)
    {
        enum PosFilterResult r1, r5;
        // Spec case: a 30 m outlier at HDOP 1 must move the estimate >= 3x more
        // than at HDOP 5. 30 m is inside POS_KF_GATE_MIN_M, so both are accepted.
        double g1 = outlierMove(modes[k], 1.0f, 30.0, &r1);
        double g5 = outlierMove(modes[k], 5.0f, 30.0, &r5);
        printf("30 m: hdop1 move=%.3f hdop5 move=%.3f ratio=%.2f\n", g1, g5, g1 / g5);
        TEST_ASSERT_EQUAL_INT(POS_KF_OK, r1);
        TEST_ASSERT_EQUAL_INT(POS_KF_OK, r5);
        TEST_ASSERT_TRUE_MESSAGE(g5 > 0.0, "hdop 5 sample must still move the estimate");
        TEST_ASSERT_TRUE_MESSAGE(g1 >= 3.0 * g5, "30 m: hdop 1 must move the estimate >= 3x more than hdop 5");

        double m1 = outlierMove(modes[k], 1.0f, 5.0, &r1);
        double m5 = outlierMove(modes[k], 5.0f, 5.0, &r5);
        printf("5 m: hdop1 move=%.3f hdop5 move=%.3f ratio=%.2f\n", m1, m5, m1 / m5);
        TEST_ASSERT_EQUAL_INT(POS_KF_OK, r1);
        TEST_ASSERT_EQUAL_INT(POS_KF_OK, r5);
        TEST_ASSERT_TRUE_MESSAGE(m5 > 0.0, "hdop 5 sample must still move the estimate");
        TEST_ASSERT_TRUE_MESSAGE(m1 >= 3.0 * m5, "5 m: hdop 1 must move the estimate >= 3x more than hdop 5");
    }
}

// ---------------------------------------------------------------------------
// 4. HDOP floor and invalid HDOP
// ---------------------------------------------------------------------------

void test_hdop_floor_und_ungueltig(void)
{
    struct PosFilter a, b;
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));   // memcmp below must not compare padding
    posFilterReset(&a); posFilterReset(&b);
    struct Rng r; rngInit(&r, 77);
    for (int i = 0; i < 30; i++)
    {
        double x = rngGauss(&r, 3), y = rngGauss(&r, 3);
        upd(&a, x, y, 0.3f, false, 0, 0, POS_KF_MODE_FIXED, DT1);
        upd(&b, x, y, 0.8f, false, 0, 0, POS_KF_MODE_FIXED, DT1);
    }
    TEST_ASSERT_EQUAL_INT(0, memcmp(&a, &b, sizeof(a)));

    struct PosFilter before;
    memcpy(&before, &a, sizeof(a));
    float bad[3] = { 0.0f, -1.0f, NAN };
    for (int i = 0; i < 3; i++)
    {
        TEST_ASSERT_EQUAL_INT(POS_KF_SKIP, upd(&a, 5, 5, bad[i], false, 0, 0, POS_KF_MODE_FIXED, DT1));
        TEST_ASSERT_EQUAL_INT(0, memcmp(&a, &before, sizeof(a)));
    }

    struct PosFilter c; posFilterReset(&c);
    TEST_ASSERT_EQUAL_INT(POS_KF_SKIP, upd(&c, 5, 5, 0.0f, false, 0, 0, POS_KF_MODE_FIXED, DT1));
    TEST_ASSERT_FALSE(c.init);
}

// ---------------------------------------------------------------------------
// 5. Gate and time-based re-seed (1 s and 3 s cadence)
// ---------------------------------------------------------------------------

static void gateRun(uint32_t dt)
{
    struct PosFilter f; posFilterReset(&f);
    for (int i = 0; i < 20; i++)
        upd(&f, 0, 0, 1.5f, false, 0, 0, POS_KF_MODE_FIXED, dt);

    uint32_t acc = 0;
    int rejected = 0;
    bool reseeded = false;
    for (int i = 0; i < 100 && !reseeded; i++)
    {
        bool ok = (upd(&f, 0, 500, 1.5f, false, 0, 0, POS_KF_MODE_FIXED, dt) == POS_KF_RESEED);
        acc += dt;
        double x, y;
        outM(&f, &x, &y);
        if (!ok)
        {
            rejected++;
            TEST_ASSERT_TRUE_MESSAGE(acc < POS_KF_RESEED_MS, "rejected past the re-seed time");
            TEST_ASSERT_TRUE_MESSAGE(fabs(y) < 5.0, "estimate must stay while rejecting");
        }
        else
        {
            reseeded = true;
            TEST_ASSERT_TRUE_MESSAGE(acc >= POS_KF_RESEED_MS, "re-seeded before 60 s");
            TEST_ASSERT_TRUE(fabs((500.0) - (y)) <= (0.01));
        }
    }
    TEST_ASSERT_TRUE(reseeded);
    TEST_ASSERT_EQUAL_INT((int)(POS_KF_RESEED_MS / dt) - 1, rejected);
}

void test_gate_und_zeitbasiertes_reseed(void)
{
    gateRun(DT1);
    gateRun(DT3);
}

// ---------------------------------------------------------------------------
// 6. Constant velocity: 50 km/h, noise, velocity measurement
// ---------------------------------------------------------------------------

void test_konstante_geschwindigkeit(void)
{
    struct Rng r; rngInit(&r, 4242);
    struct PosFilter f; posFilterReset(&f);
    const double v = 50.0 / 3.6, crs = 45.0 * PI_D / 180.0;
    double ve = v * sin(crs), vn = v * cos(crs);
    double lastAlong = 0;

    for (int t = 0; t <= 30; t++)
    {
        double x = ve * t, y = vn * t;
        float spd = (float)(v + rngGauss(&r, 0.2));
        upd(&f, x + rngGauss(&r, 4.0), y + rngGauss(&r, 4.0), 1.5f, true, spd,
            (float)(45.0 + rngGauss(&r, 1.0)), POS_KF_MODE_TRACK, DT1);
        double fx, fy;
        outM(&f, &fx, &fy);
        lastAlong = (fx - x) * sin(crs) + (fy - y) * cos(crs);
    }
    printf("along-track error at 30 s: %.2f m, v=(%.2f,%.2f) true (%.2f,%.2f)\n",
           lastAlong, f.e.v, f.n.v, ve, vn);
    TEST_ASSERT_TRUE_MESSAGE(fabs(lastAlong) < 15.0, "along-track error >= 15 m after 30 s");
    TEST_ASSERT_TRUE_MESSAGE(fabs(f.e.v - ve) < 0.5 && fabs(f.n.v - vn) < 0.5,
                             "velocity estimate must follow the Doppler velocity");
}

// ---------------------------------------------------------------------------
// 7. Turn: 90 deg at 30 km/h, overshoot past the corner
// ---------------------------------------------------------------------------

void test_kurve_ueberschwingen(void)
{
    struct Rng r; rngInit(&r, 9001);
    struct PosFilter f; posFilterReset(&f);
    const double v = 30.0 / 3.6;
    const int tc = 20;                 // corner at t = 20 s
    const double cx = v * tc;
    double maxOver = 0;

    for (int t = 0; t <= 45; t++)
    {
        double x, y; float crs;
        if (t <= tc) { x = v * t; y = 0; crs = 90.0f; }
        else         { x = cx; y = v * (t - tc); crs = 0.0f; }
        upd(&f, x + rngGauss(&r, 3.0), y + rngGauss(&r, 3.0), 1.2f, true,
            (float)v, crs, POS_KF_MODE_TRACK, DT1);
        double fx, fy;
        outM(&f, &fx, &fy);
        if (t > tc && fx - cx > maxOver)
            maxOver = fx - cx;
    }
    printf("turn overshoot: %.2f m\n", maxOver);
    TEST_ASSERT_TRUE_MESSAGE(maxOver < 20.0, "overshoot past the corner >= 20 m");
}

// ---------------------------------------------------------------------------
// 8. Cadence independence: same trajectory at 1 s and 3 s
// ---------------------------------------------------------------------------

static void cadenceRun(uint32_t dt, uint32_t seed, double *ex, double *ey, double *tx, double *ty)
{
    struct Rng r; rngInit(&r, seed);
    struct PosFilter f; posFilterReset(&f);
    const double v = 10.0, crs = 30.0 * PI_D / 180.0;
    double x = 0, y = 0;
    for (uint32_t t = 0; t <= 120000; t += dt)
    {
        x = v * sin(crs) * t / 1000.0;
        y = v * cos(crs) * t / 1000.0;
        upd(&f, x + rngGauss(&r, 3.0), y + rngGauss(&r, 3.0), 1.1f, true,
            (float)(v + rngGauss(&r, 0.2)), (float)(30.0 + rngGauss(&r, 1.0)),
            POS_KF_MODE_TRACK, t == 0 ? 0 : dt);
    }
    outM(&f, ex, ey);
    *tx = x; *ty = y;
}

void test_kadenz_unabhaengig(void)
{
    double e1x, e1y, t1x, t1y, e3x, e3y, t3x, t3y;
    cadenceRun(DT1, 11, &e1x, &e1y, &t1x, &t1y);
    cadenceRun(DT3, 22, &e3x, &e3y, &t3x, &t3y);
    double d = hypot(e1x - e3x, e1y - e3y);
    printf("cadence: 1s err=%.2f 3s err=%.2f diff=%.2f m\n",
           hypot(e1x - t1x, e1y - t1y), hypot(e3x - t3x, e3y - t3y), d);
    TEST_ASSERT_TRUE_MESSAGE(d < 2.0, "1 s and 3 s runs end more than 2 m apart");
}

// ---------------------------------------------------------------------------
// 9. dt clamp
// ---------------------------------------------------------------------------

void test_dt_klemmung(void)
{
    struct PosFilter a, b;
    posFilterReset(&a); posFilterReset(&b);
    upd(&a, 0, 0, 1.5f, false, 0, 0, POS_KF_MODE_TRACK, 0);
    upd(&b, 0, 0, 1.5f, false, 0, 0, POS_KF_MODE_TRACK, 0);
    upd(&a, 1, 1, 1.5f, false, 0, 0, POS_KF_MODE_TRACK, POS_KF_DT_MAX_MS);
    TEST_ASSERT_EQUAL_INT(POS_KF_RESEED, upd(&b, 1, 1, 1.5f, false, 0, 0, POS_KF_MODE_TRACK, 600000u));
    TEST_ASSERT_TRUE(isfinite(b.e.Ppp));
    TEST_ASSERT_TRUE(isfinite(a.e.Ppp));
    // the 600 s gap re-seeds: P is the seed value, not 600 s of process noise
    TEST_ASSERT_TRUE(b.e.Ppp <= 20.0f);
    TEST_ASSERT_TRUE(b.e.Pvv <= POS_KF_P_VV0);
    // a gap longer than the clamp re-seeds on the sample
    double x, y;
    outM(&b, &x, &y);
    TEST_ASSERT_TRUE(fabs((1.0) - (x)) <= (0.01));
}

// ---------------------------------------------------------------------------
// 10. Re-anchor: 20 km east drive, no jump
// ---------------------------------------------------------------------------

void test_reanchor_ohne_sprung(void)
{
    struct PosFilter f; posFilterReset(&f);
    const double v = 20.0;
    int anchors = 0;
    double lastLon0 = 0, maxErr = 0;

    for (int t = 0; t <= 1000; t++)
    {
        double x = v * t;
        upd(&f, x, 0, 1.0f, true, (float)v, 90.0f, POS_KF_MODE_TRACK, t == 0 ? 0 : DT1);
        if (t > 0 && f.lon0 != lastLon0) anchors++;
        lastLon0 = f.lon0;
        double fx, fy;
        outM(&f, &fx, &fy);
        double err = hypot(fx - x, fy);
        if (err > maxErr) maxErr = err;
    }
    printf("re-anchors=%d max err=%.4f m\n", anchors, maxErr);
    TEST_ASSERT_TRUE(anchors >= 3);
    TEST_ASSERT_TRUE_MESSAGE(maxErr < 0.5, "output jumped at a re-anchor");
}

// ---------------------------------------------------------------------------
// 11. Hemispheres and anti-meridian
// ---------------------------------------------------------------------------

void test_hemisphaeren_und_antimeridian(void)
{
    double lats[3] = { -33.9, -33.9, 40.0 };
    double lons[3] = { 151.2, -70.7, -105.3 };
    for (int i = 0; i < 3; i++)
    {
        struct PosFilter f; posFilterReset(&f);
        posFilterUpdate(&f, lats[i], lons[i], 1.5f, false, 0, 0, POS_KF_MODE_FIXED, 0);
        posFilterUpdate(&f, lats[i] + 0.00001, lons[i] - 0.00001, 1.5f, false, 0, 0,
                        POS_KF_MODE_FIXED, DT1);
        double la, lo;
        posFilterGet(&f, &la, &lo);
        TEST_ASSERT_TRUE(fabs((lats[i]) - (la)) <= (0.0001));
        TEST_ASSERT_TRUE(fabs((lons[i]) - (lo)) <= (0.0001));
    }

    // Crossing +-180 is not wrapped: the step looks like a ~40000 km jump, is
    // rejected, the estimate stays, and the filter re-seeds after 60 s.
    struct PosFilter f; posFilterReset(&f);
    posFilterUpdate(&f, 10.0, 179.9999, 1.5f, false, 0, 0, POS_KF_MODE_FIXED, 0);
    TEST_ASSERT_EQUAL_INT(POS_KF_REJECT, posFilterUpdate(&f, 10.0, -179.9999, 1.5f, false, 0, 0, POS_KF_MODE_FIXED, DT1));
    double la, lo;
    posFilterGet(&f, &la, &lo);
    TEST_ASSERT_TRUE(lo > 179.0);
    bool ok = false;
    for (int i = 0; i < 70 && !ok; i++)
        ok = (posFilterUpdate(&f, 10.0, -179.9999, 1.5f, false, 0, 0, POS_KF_MODE_FIXED, DT1) == POS_KF_RESEED);
    TEST_ASSERT_TRUE(ok);
    posFilterGet(&f, &la, &lo);
    TEST_ASSERT_TRUE(fabs((-179.9999) - (lo)) <= (1e-6));
}

// ---------------------------------------------------------------------------
// 12. Zero-velocity update
// ---------------------------------------------------------------------------

void test_zero_velocity_update(void)
{
    struct Rng r; rngInit(&r, 31337);
    struct PosFilter f; posFilterReset(&f);
    double maxV = 0;
    for (int t = 0; t < 300; t++)
    {
        float crs = (float)(rngUniform(&r) * 360.0);
        upd(&f, rngGauss(&r, 4.0), rngGauss(&r, 4.0), 1.5f, true, 0.2f, crs,
            POS_KF_MODE_TRACK, t == 0 ? 0 : DT1);
        if (t >= 30)
        {
            double s = hypot(f.e.v, f.n.v);
            if (s > maxV) maxV = s;
        }
    }
    printf("zero-velocity: max |v| = %.3f m/s\n", maxV);
    TEST_ASSERT_TRUE_MESSAGE(maxV < 0.05, "velocity estimate must stay under 0.05 m/s");
}

// ---------------------------------------------------------------------------
// 13. Step held for 120 s: no re-seed, no output step (both modes, 1 s and 3 s)
// ---------------------------------------------------------------------------

// A real 15 m position change at HDOP 1.0 (R = 8 m^2): inside POS_KF_GATE_MIN_M,
// so it must be absorbed smoothly. Without the minimum gate distance the sample
// is d2-rejected for 60 s and the filter then re-seeds with a 15 m jump.
static void stepRun(enum PosFilterMode m, uint32_t dt)
{
    struct Rng r; rngInit(&r, 515);
    struct PosFilter f; posFilterReset(&f);
    for (uint32_t t = 0; t < 60000; t += dt)
        upd(&f, rngGauss(&r, 1.5), rngGauss(&r, 1.5), 1.0f, true, 0.0f, 0, m, t == 0 ? 0 : dt);

    double px, py, maxStep = 0;
    outM(&f, &px, &py);
    for (uint32_t t = dt; t <= 120000; t += dt)
    {
        enum PosFilterResult res = upd(&f, 15.0 + rngGauss(&r, 1.5), rngGauss(&r, 1.5), 1.0f,
                                       true, 0.0f, 0, m, dt);
        TEST_ASSERT_EQUAL_INT_MESSAGE(POS_KF_OK, res, "a 15 m step must be accepted, not gated");
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, f.rejects, "no reject, so no re-seed timer");
        double x, y;
        outM(&f, &x, &y);
        double st = hypot(x - px, y - py);
        if (st > maxStep) maxStep = st;
        px = x; py = y;
    }
    printf("step 15 m, %s dt=%u: max output step %.2f m, end x=%.2f m\n",
           m == POS_KF_MODE_FIXED ? "FIXED" : "TRACK", (unsigned)dt, maxStep, px);
    TEST_ASSERT_TRUE_MESSAGE(maxStep <= 5.0, "output step > 5 m between consecutive outputs");
    TEST_ASSERT_TRUE_MESSAGE(px > 5.0, "the new position must pull the estimate");
}

void test_sprung_15m_ohne_reseed(void)
{
    stepRun(POS_KF_MODE_FIXED, DT1);
    stepRun(POS_KF_MODE_FIXED, DT3);
    stepRun(POS_KF_MODE_TRACK, DT1);
    stepRun(POS_KF_MODE_TRACK, DT3);
}

// ---------------------------------------------------------------------------
// 14. The reject timer restarts on an accepted sample
// ---------------------------------------------------------------------------

// reject (40 s), accept, reject (40 s): 80 s in total but never 60 s in a row.
// A timer that is not cleared by the accept re-seeds in the second block.
static void rejectResetRun(enum PosFilterMode m, uint32_t dt)
{
    struct PosFilter f; posFilterReset(&f);
    for (int i = 0; i < 20; i++)
        upd(&f, 0, 0, 1.5f, true, 0.0f, 0, m, i == 0 ? 0 : dt);

    int n = (int)(40000u / dt);
    for (int blk = 0; blk < 2; blk++)
    {
        for (int i = 0; i < n; i++)
            TEST_ASSERT_EQUAL_INT_MESSAGE(POS_KF_REJECT, upd(&f, 0, 500, 1.5f, true, 0.0f, 0, m, dt),
                                          "still inside the 60 s window: must stay rejected");
        TEST_ASSERT_EQUAL_UINT32((uint32_t)n * dt, f.reject_ms);
        if (blk == 0)
        {
            TEST_ASSERT_EQUAL_INT(POS_KF_OK, upd(&f, 0, 0, 1.5f, true, 0.0f, 0, m, dt));
            TEST_ASSERT_EQUAL_UINT32(0, f.reject_ms);
        }
    }
    double x, y;
    outM(&f, &x, &y);
    TEST_ASSERT_TRUE_MESSAGE(fabs(y) < 5.0, "estimate jumped: the reject timer was not reset by the accept");
}

void test_reject_timer_wird_bei_accept_zurueckgesetzt(void)
{
    rejectResetRun(POS_KF_MODE_FIXED, DT1);
    rejectResetRun(POS_KF_MODE_FIXED, DT3);
    rejectResetRun(POS_KF_MODE_TRACK, DT1);
    rejectResetRun(POS_KF_MODE_TRACK, DT3);
}

// ---------------------------------------------------------------------------
// 15. FIXED mode, correlated multipath: AR(1) tau 90 s, 10 m per axis + 2 m white
// ---------------------------------------------------------------------------

static void corrRun(uint32_t dt, uint32_t seed, double *rawRms, double *filtRms, int *rejects)
{
    struct Rng r; rngInit(&r, seed);
    struct PosFilter f; posFilterReset(&f);
    const double a = exp(-(double)dt / 90000.0);
    const double sd = 10.0 * sqrt(1.0 - a * a);
    double cx = rngGauss(&r, 10.0), cy = rngGauss(&r, 10.0);
    double sr = 0, sf = 0;
    int n = 0;
    *rejects = 0;

    for (uint32_t t = 0; t <= 7200000u; t += dt)
    {
        cx = a * cx + rngGauss(&r, sd);
        cy = a * cy + rngGauss(&r, sd);
        double mx = cx + rngGauss(&r, 2.0), my = cy + rngGauss(&r, 2.0);
        enum PosFilterResult res = upd(&f, mx, my, 1.0f, true, 0.05f,
                                       (float)(rngUniform(&r) * 360.0), POS_KF_MODE_FIXED,
                                       t == 0 ? 0 : dt);
        if (t > 0 && res != POS_KF_OK) (*rejects)++;   /* a reject or a re-seed */
        if (t >= 600000u)
        {
            double fx, fy;
            outM(&f, &fx, &fy);
            sr += mx * mx + my * my;
            sf += fx * fx + fy * fy;
            n++;
        }
    }
    *rawRms  = sqrt(sr / n);
    *filtRms = sqrt(sf / n);
}

// One realisation's ratio scatters (p96 near 0.4 over 100 seeds), so the
// assertion is on the mean of ten seeds per cadence.
#define CORR_SEEDS 10

void test_fixed_korreliertes_rauschen(void)
{
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
    {
        double sumRatio = 0;
        for (int s = 0; s < CORR_SEEDS; s++)
        {
            double raw, filt;
            int rej;
            corrRun(dts[k], 0x1234ABCDu + 7919u * (uint32_t)s + (uint32_t)k, &raw, &filt, &rej);
            TEST_ASSERT_TRUE_MESSAGE(raw > 10.0, "raw rms must reflect 10 m correlated + 2 m white per axis");
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, rej, "correlated multipath must not trigger a reject/re-seed");
            sumRatio += filt / raw;
        }
        double mean = sumRatio / CORR_SEEDS;
        printf("FIXED correlated dt=%u: mean ratio over %d seeds %.3f\n", (unsigned)dts[k], CORR_SEEDS, mean);
        TEST_ASSERT_TRUE_MESSAGE(mean <= 0.35, "mean filtered/raw rms must be <= 0.35 after 10 min");
    }
}

// The same 1 s trajectory, fed at 1 s and as its own 3 s subsample: the FIXED
// average must not depend on the cadence (goal G5).
void test_fixed_kadenz_teilstichprobe(void)
{
    enum { N = 7200 };
    static double xs[N], ys[N];
    struct Rng r; rngInit(&r, 0xC0FFEEu);
    const double a = exp(-1.0 / 90.0), sd = 10.0 * sqrt(1.0 - a * a);
    double cx = rngGauss(&r, 10.0), cy = rngGauss(&r, 10.0);
    for (int i = 0; i < N; i++)
    {
        cx = a * cx + rngGauss(&r, sd);
        cy = a * cy + rngGauss(&r, sd);
        xs[i] = cx + rngGauss(&r, 2.0);
        ys[i] = cy + rngGauss(&r, 2.0);
    }

    struct PosFilter f1, f3;
    posFilterReset(&f1); posFilterReset(&f3);
    double sd2 = 0;
    int n = 0;
    for (int i = 0; i < N; i++)
    {
        upd(&f1, xs[i], ys[i], 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED, i == 0 ? 0 : DT1);
        if (i % 3 == 0)
        {
            upd(&f3, xs[i], ys[i], 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED, i == 0 ? 0 : DT3);
            if (i >= 600)
            {
                double x1, y1, x3, y3;
                outM(&f1, &x1, &y1);
                outM(&f3, &x3, &y3);
                sd2 += (x1 - x3) * (x1 - x3) + (y1 - y3) * (y1 - y3);
                n++;
            }
        }
    }
    double rms = sqrt(sd2 / n);
    printf("FIXED 1 s vs 3 s subsample: rms output difference %.3f m\n", rms);
    TEST_ASSERT_TRUE_MESSAGE(rms < 0.5, "1 s and 3 s FIXED outputs differ by >= 0.5 m rms");
}

// A window-sill node loses its fix for longer than POS_KF_DT_MAX_MS: FIXED must
// keep its average, not re-seed onto the first sample after the gap.
void test_fixed_luecke_ohne_reseed(void)
{
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
    {
        struct Rng r; rngInit(&r, 4242u + (uint32_t)k);
        struct PosFilter f; posFilterReset(&f);
        for (uint32_t t = 0; t <= 7200000u; t += dts[k])
            upd(&f, rngGauss(&r, 6.0), rngGauss(&r, 6.0), 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED,
                t == 0 ? 0 : dts[k]);

        double bx, by, ax, ay;
        outM(&f, &bx, &by);
        // first sample after the gap, 12 m off: a re-seed would jump onto it
        enum PosFilterResult res = upd(&f, 12.0, -5.0, 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED,
                                       POS_KF_DT_MAX_MS + 1000u);
        outM(&f, &ax, &ay);
        double step = hypot(ax - bx, ay - by);
        printf("FIXED gap dt=%u: result %d, output step %.3f m\n", (unsigned)dts[k], (int)res, step);
        TEST_ASSERT_EQUAL_INT_MESSAGE(POS_KF_OK, res, "a FIXED gap must not re-seed");
        TEST_ASSERT_TRUE_MESSAGE(step < 0.5, "output step after a FIXED gap >= 0.5 m");
    }
}

// ---------------------------------------------------------------------------
// 16. Mode switch: no reset, output continuous
// ---------------------------------------------------------------------------

// A long converged run in one mode, then the sample at the switch carries an
// innovation of 2 m. A reset onto the sample would step the output by 2 m; the
// continued filter moves it by its gain times 2 m.
static void switchRun(enum PosFilterMode from, enum PosFilterMode to, double *stepOut)
{
    struct Rng r; rngInit(&r, 8080);
    struct PosFilter f; posFilterReset(&f);
    for (int t = 0; t < 900; t++)
        upd(&f, 20.0 + rngGauss(&r, 3.0), -10.0 + rngGauss(&r, 3.0), 2.0f, true, 0.0f, 0, from,
            t == 0 ? 0 : DT1);

    double bx, by;
    outM(&f, &bx, &by);
    enum PosFilterResult res = upd(&f, bx + 2.0, by, 2.0f, false, 0, 0, to, DT1);
    TEST_ASSERT_EQUAL_INT(POS_KF_OK, res);
    double ax, ay;
    outM(&f, &ax, &ay);
    *stepOut = hypot(ax - bx, ay - by);

    // and it keeps running in the new mode without a jump or a reject
    double px = ax, py = ay, maxStep = 0;
    for (int t = 0; t < 60; t++)
    {
        res = upd(&f, 20.0 + rngGauss(&r, 3.0), -10.0 + rngGauss(&r, 3.0), 2.0f, true, 0.0f, 0, to, DT1);
        TEST_ASSERT_EQUAL_INT(POS_KF_OK, res);
        double x, y;
        outM(&f, &x, &y);
        double st = hypot(x - px, y - py);
        if (st > maxStep) maxStep = st;
        px = x; py = y;
    }
    TEST_ASSERT_TRUE_MESSAGE(maxStep <= 5.0, "output step > 5 m after the switch");
    TEST_ASSERT_TRUE_MESSAGE(fabs(px - 20.0) < 8.0 && fabs(py + 10.0) < 8.0, "estimate lost the position");
}

void test_moduswechsel_stetig(void)
{
    double s1, s2;
    switchRun(POS_KF_MODE_FIXED, POS_KF_MODE_TRACK, &s1);
    switchRun(POS_KF_MODE_TRACK, POS_KF_MODE_FIXED, &s2);
    printf("mode switch step: FIXED->TRACK %.3f m, TRACK->FIXED %.3f m\n", s1, s2);
    TEST_ASSERT_TRUE_MESSAGE(s1 < 0.5, "FIXED->TRACK stepped the output");
    TEST_ASSERT_TRUE_MESSAGE(s2 < 0.5, "TRACK->FIXED stepped the output");

    // FIXED (still) holds v = 0 even when the receiver reports a speed: below
    // POS_KF_STILL_MPS it is noise. A speed >= STILL for 5 s is no longer ignored
    // since rework 3 (the node is declared moving and runs the CV model, see
    // test_fixed_teilzustand_vertrag), so this case reports 0.3 m/s, not 8 m/s.
    struct PosFilter f; posFilterReset(&f);
    upd(&f, 0, 0, 1.0f, true, 0.3f, 90.0f, POS_KF_MODE_FIXED, 0);
    for (int t = 0; t < 20; t++)
        upd(&f, 0, 0, 1.0f, true, 0.3f, 90.0f, POS_KF_MODE_FIXED, DT1);
    TEST_ASSERT_TRUE(posFilterIsStill(&f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.e.v);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.n.v);

    // two fast samples (8 m/s) are speed noise below the evidence threshold:
    // still, and the FIXED model ignores the velocity
    for (int t = 0; t < 2; t++)
        upd(&f, 0, 0, 1.0f, true, 8.0f, 90.0f, POS_KF_MODE_FIXED, DT1);
    TEST_ASSERT_TRUE(posFilterIsStill(&f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.e.v);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, f.n.v);
}

// ---------------------------------------------------------------------------
// 17. vel_valid contract: speed new; course only read from POS_KF_STILL_MPS up
// ---------------------------------------------------------------------------

void test_vel_valid_zupt_ohne_kurs(void)
{
    struct PosFilter a, b;
    // both start at 5 m/s east, then stand still
    posFilterSeed(&a, A_LAT, A_LON, 1.0f, true, 5.0f, 90.0f);
    posFilterSeed(&b, A_LAT, A_LON, 1.0f, true, 5.0f, 90.0f);
    for (int t = 0; t < 8; t++)
    {
        // a: speed 0.1 m/s is new, the course is invalid (NaN): the ZUPT must still run
        upd(&a, 0, 0, 1.0f, true, 0.1f, NAN, POS_KF_MODE_TRACK, DT1);
        // b: no velocity information at all
        upd(&b, 0, 0, 1.0f, false, 0, 0, POS_KF_MODE_TRACK, DT1);
    }
    printf("ZUPT without course: |v| a=%.3f b=%.3f\n", hypot(a.e.v, a.n.v), hypot(b.e.v, b.n.v));
    TEST_ASSERT_TRUE(isfinite(a.e.p) && isfinite(a.n.p) && isfinite(a.e.v) && isfinite(a.n.v));
    TEST_ASSERT_TRUE_MESSAGE(hypot(a.e.v, a.n.v) < 0.05, "ZUPT with an invalid course must pull v to 0");
    TEST_ASSERT_TRUE_MESSAGE(hypot(a.e.v, a.n.v) < hypot(b.e.v, b.n.v), "ZUPT must beat no velocity update");

    // above STILL an invalid course makes the velocity unusable: ignored, state stays finite
    upd(&a, 40, 0, 1.0f, true, 8.0f, NAN, POS_KF_MODE_TRACK, DT1);
    upd(&a, 40, 0, 1.0f, true, NAN, 90.0f, POS_KF_MODE_TRACK, DT1);
    TEST_ASSERT_TRUE(isfinite(a.e.p) && isfinite(a.n.p) && isfinite(a.e.v) && isfinite(a.n.v));
    TEST_ASSERT_TRUE(isfinite(a.e.Ppp) && isfinite(a.e.Pvv));
}

// ---------------------------------------------------------------------------
// 18. FIXED motion detection (rework 3): a walker with `--track off`
// ---------------------------------------------------------------------------

// Stand 600 s, then walk east at v m/s until walkEnd (stops: stand the first
// `stopSec` of every minute), multipath as in the field model: AR(1) 5 m per
// axis, tau 60 s, + 2 m white. staticHold: the receiver reports speed 0 below
// 1 m/s (a static-hold receiver); else the speed is truthful, +N(0, 0.1).
struct WalkCfg
{
    double   v;
    bool     staticHold;
    uint32_t dt;
    double   hdop;
    uint32_t seed;
    double   stopSec;
    double   walkEnd;    // s; 1e9 = never stop
    double   total;      // s
};

#define WALK_MAX 3700
static double g_wx[WALK_MAX], g_wy[WALK_MAX];   // filter output per second index (dt 1 s grid)
static double g_rx[WALK_MAX], g_ry[WALK_MAX];   // the raw samples of the same seconds

struct WalkRes
{
    double detect;       // s after the walk start until the first still -> moving (-1: never)
    double lag60, lag300;
    int    reseeds, rejects;
    double maxStep;
    double toStill;      // s after walkEnd until posFilterIsStill() again (-1: never)
    int    nMoving, nStill;
    double range;        // max |output - mean| over the 30 min after toStill + 60 s
    double range5;       // the same, from toStill + 300 s on (the resumed average has grown)
    double rawRange;     // the same window (+60 s) for the raw samples: what the output is compared with
    bool   stillAtEnd;
};

static WalkRes walkRun(const WalkCfg &c)
{
    struct Rng r; rngInit(&r, c.seed);
    struct PosFilter f; posFilterReset(&f);
    WalkRes o = { -1, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, false };
    const double a = exp(-((double)c.dt / 1000.0) / 60.0), sd = 5.0 * sqrt(1.0 - a * a);
    double cx = rngGauss(&r, 5.0), cy = rngGauss(&r, 5.0);
    double pos = 0, px = 0, py = 0;
    int nOut = 0;

    for (uint32_t t = 0; t <= (uint32_t)(c.total * 1000.0); t += c.dt)
    {
        double ts = t / 1000.0;
        double vt = (ts < 600.0) ? 0.0 : (ts < c.walkEnd ? c.v : 0.0);
        if (c.stopSec > 0 && ts >= 600.0 && ts < c.walkEnd && fmod(ts, 60.0) < c.stopSec) vt = 0.0;
        pos += vt * c.dt / 1000.0;
        cx = a * cx + rngGauss(&r, sd);
        cy = a * cy + rngGauss(&r, sd);
        double mx = pos + cx + rngGauss(&r, 2.0), my = cy + rngGauss(&r, 2.0);
        double spd = c.staticHold ? (vt < 1.0 ? 0.0 : vt) : fabs(vt + rngGauss(&r, 0.1));
        float  crs = (float)(90.0 + rngGauss(&r, 10.0));
        enum PosFilterResult res = upd(&f, mx, my, (float)c.hdop, true, (float)spd, crs,
                                       POS_KF_MODE_FIXED, t == 0 ? 0 : c.dt);
        if (t > 0 && res == POS_KF_RESEED) o.reseeds++;
        if (res == POS_KF_REJECT) o.rejects++;

        double fx, fy;
        outM(&f, &fx, &fy);
        if (t > 0)
        {
            double st = hypot(fx - px, fy - py);
            if (st > o.maxStep) o.maxStep = st;
        }
        px = fx; py = fy;
        if (o.detect < 0 && f.n_moving > 0 && ts >= 600.0) o.detect = ts - 600.0;
        if (fabs(ts - 660.0) < c.dt / 2000.0 + 1e-9) o.lag60 = pos - fx;
        if (fabs(ts - 900.0) < c.dt / 2000.0 + 1e-9) o.lag300 = pos - fx;
        if (c.walkEnd < 1e8 && ts >= c.walkEnd && o.toStill < 0 && posFilterIsStill(&f))
            o.toStill = ts - c.walkEnd;
        if (c.dt == DT1 && t / 1000 < WALK_MAX)
        {
            g_wx[t / 1000] = fx; g_wy[t / 1000] = fy;
            g_rx[t / 1000] = mx; g_ry[t / 1000] = my;
            nOut = (int)(t / 1000) + 1;
        }
        o.nMoving = f.n_moving; o.nStill = f.n_still;
        o.stillAtEnd = posFilterIsStill(&f);
    }

    if (c.dt == DT1 && o.toStill >= 0)
    {
        double *rg[3] = { &o.range, &o.range5, &o.rawRange };
        for (int w = 0; w < 3; w++)
        {
            const double *sx = (w == 2) ? g_rx : g_wx, *sy = (w == 2) ? g_ry : g_wy;
            int i0 = (int)(c.walkEnd + o.toStill + (w == 1 ? 300.0 : 60.0)), i1 = i0 + 1800;
            if (i1 > nOut) i1 = nOut;
            double mx = 0, my = 0;
            for (int i = i0; i < i1; i++) { mx += sx[i]; my += sy[i]; }
            mx /= (i1 - i0); my /= (i1 - i0);
            for (int i = i0; i < i1; i++)
            {
                double d = hypot(sx[i] - mx, sy[i] - my);
                if (d > *rg[w]) *rg[w] = d;
            }
        }
    }
    return o;
}

static const uint32_t WALK_SEEDS[2] = { 7u, 11u };

// T1: truthful receiver speed. Declared moving within 15 s, no lag, no jump.
void test_fixed_gehen_wahrheitsgemaess(void)
{
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
        for (int s = 0; s < 2; s++)
        {
            WalkCfg c = { 0.83, false, dts[k], 1.2, WALK_SEEDS[s], 0, 1e9, 2400 };
            WalkRes o = walkRun(c);
            printf("T1 dt=%u seed=%u: detect %.0f s, lag60 %.1f, lag300 %.1f, reseeds %d, max step %.1f m\n",
                   (unsigned)dts[k], (unsigned)WALK_SEEDS[s], o.detect, o.lag60, o.lag300, o.reseeds, o.maxStep);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, o.reseeds, "walking with --track off must not re-seed");
            TEST_ASSERT_TRUE_MESSAGE(o.maxStep < 10.0, "max output step >= 10 m while walking");
            TEST_ASSERT_TRUE_MESSAGE(o.detect >= 0 && o.detect <= 20.0, "moving not declared within 20 s");
            TEST_ASSERT_TRUE_MESSAGE(fabs(o.lag60) < 25.0, "lag at 60 s >= 25 m");
            TEST_ASSERT_TRUE_MESSAGE(fabs(o.lag300) < 25.0, "lag at 300 s >= 25 m");
            TEST_ASSERT_EQUAL_INT_MESSAGE(1, o.nMoving, "exactly one still -> moving transition");
        }
}

// T2: static-hold receiver (speed 0 below 1 m/s): only the displacement
// criterion sees the walk. 0.83 m/s needs 40 m / 0.83 = 48 s to reach the
// radius, + the smoothing lag + the hold: ~75 s, so the bound is 90 s.
void test_fixed_gehen_static_hold(void)
{
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
        for (int s = 0; s < 2; s++)
        {
            WalkCfg c = { 0.83, true, dts[k], 1.2, WALK_SEEDS[s], 0, 1e9, 2400 };
            WalkRes o = walkRun(c);
            printf("T2 dt=%u seed=%u: detect %.0f s, lag60 %.1f, lag300 %.1f, reseeds %d, rejects %d, max step %.1f m\n",
                   (unsigned)dts[k], (unsigned)WALK_SEEDS[s], o.detect, o.lag60, o.lag300, o.reseeds, o.rejects, o.maxStep);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, o.reseeds, "a static-hold walker must not re-seed");
            TEST_ASSERT_TRUE_MESSAGE(o.maxStep < 15.0, "max output step >= 15 m");
            TEST_ASSERT_TRUE_MESSAGE(o.detect >= 0 && o.detect <= 90.0, "moving not declared within 90 s");
            TEST_ASSERT_TRUE_MESSAGE(fabs(o.lag300) < 40.0, "lag at 300 s >= 40 m");
        }
}

// T3: dog walk, 2 km/h with a 20 s stop every minute: once moving, never back
// to still (neither the stops nor the speed-0 samples may release it).
void test_fixed_hundespaziergang_bleibt_moving(void)
{
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
        for (int s = 0; s < 2; s++)
        {
            WalkCfg c = { 0.55, false, dts[k], 1.2, WALK_SEEDS[s], 20, 1e9, 2400 };
            WalkRes o = walkRun(c);
            printf("T3 dt=%u seed=%u: detect %.0f s, still transitions %d, reseeds %d, max step %.1f m\n",
                   (unsigned)dts[k], (unsigned)WALK_SEEDS[s], o.detect, o.nStill, o.reseeds, o.maxStep);
            TEST_ASSERT_TRUE_MESSAGE(o.detect >= 0, "dog walk not detected");
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, o.nStill, "returned to still during the walk");
            TEST_ASSERT_EQUAL_INT(0, o.reseeds);
            // 2 km/h = 0.55 m/s straddles POS_KF_STILL_MPS: a late declaration (87 s at
            // 3 s cadence, seed 11) catches up ~25 m in one or two steps
            TEST_ASSERT_TRUE_MESSAGE(o.maxStep < 15.0, "max output step >= 15 m");
        }
}

// T4: walk 10 min, then stand (truthful and static-hold receiver): back to
// still within 4 min, and the average resumes (small spread over 30 min).
void test_fixed_stehen_zurueck_auf_still(void)
{
    for (int m = 0; m < 3; m++)
    {
        // 0: truthful 1 s, 1: static hold 1 s, 2: truthful 3 s (no spread: dt != 1 s)
        WalkCfg c = { 0.83, m == 1, m == 2 ? DT3 : DT1, 1.2, 7u, 0, 1200, 3600 };
        WalkRes o = walkRun(c);
        if (m != 2)
            printf("T4 case %d: to still %.0f s after stopping, moving %d still %d, range over 30 min %.1f m (from +300 s: %.1f m, raw samples: %.1f m), max step %.1f m\n",
                   m, o.toStill, o.nMoving, o.nStill, o.range, o.range5, o.rawRange, o.maxStep);
        else
            printf("T4 case %d: to still %.0f s after stopping, moving %d still %d, range n/a, max step %.1f m\n",
                   m, o.toStill, o.nMoving, o.nStill, o.maxStep);
        TEST_ASSERT_TRUE_MESSAGE(o.toStill >= 0 && o.toStill <= 240.0, "not back to still within 4 min");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, o.nMoving, "moving declared more than once");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, o.nStill, "still returned more than once");
        TEST_ASSERT_TRUE(o.stillAtEnd);
        if (m != 2)
            TEST_ASSERT_TRUE_MESSAGE(o.range < 0.6 * o.rawRange, "resumed average wanders like the raw samples");
    }
}

// T5: a stationary node must not be declared moving by multipath or by noisy
// speed reports: 24 h, correlated sigma per axis, tau 90 s, + 2 m white, HDOP
// 1.0, receiver speed |N(0, sd)| with random course. Counted: still -> moving
// transitions, the time spent moving and the longest moving episode (a false
// trigger that never returns costs the grid hold for good).
struct FtRes { int nMoving; double movingShare; double longestS; };

static FtRes falseTriggerRun(double sigma, double spdSd, uint32_t dt, uint32_t seed)
{
    struct Rng r; rngInit(&r, seed);
    struct PosFilter f; posFilterReset(&f);
    const double a = exp(-(double)dt / 90000.0);
    const double sd = sigma * sqrt(1.0 - a * a);
    double cx = rngGauss(&r, sigma), cy = rngGauss(&r, sigma);
    double movingMs = 0, episode = 0, longest = 0;

    for (uint32_t t = 0; t <= 86400000u; t += dt)
    {
        cx = a * cx + rngGauss(&r, sd);
        cy = a * cy + rngGauss(&r, sd);
        upd(&f, cx + rngGauss(&r, 2.0), cy + rngGauss(&r, 2.0), 1.0f, true,
            (float)fabs(rngGauss(&r, spdSd)), (float)(rngUniform(&r) * 360.0), POS_KF_MODE_FIXED,
            t == 0 ? 0 : dt);
        if (!posFilterIsStill(&f))
        {
            movingMs += dt;
            episode  += dt;
            if (episode > longest) longest = episode;
        }
        else
            episode = 0;
    }
    FtRes o = { f.n_moving, movingMs / 86400000.0, longest / 1000.0 };
    return o;
}

struct FtCase { double sigma, spdSd; int seeds; int maxPerDay; };

void test_fixed_keine_falschen_bewegungen(void)
{
    // sigma 10 m: an ordinary bad site; 15 m: the pessimistic tail (NLOS behind
    // coated glass). Speed noise 0.2 / 0.3 m/s: the stationary speed noise of
    // the receivers is not measured yet (W3 bench).
    // The per-day bound of the 0.3 m/s cases is looser: at the 3 s cadence the
    // speed evidence of a noisy receiver triggers ~1 per day (max 3 measured);
    // each episode is short (< 2 min), which the share and episode bounds pin.
    // Retune POS_KF_MOVE_SPEED_MS from the W3 bench speed field.
    const FtCase cases[4] = { { 10.0, 0.2, 20, 2 }, { 10.0, 0.3, 10, 4 },
                              { 15.0, 0.2, 10, 3 }, { 15.0, 0.3, 5, 4 } };
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
        for (int c = 0; c < 4; c++)
        {
            int sum = 0, maxN = 0;
            double maxShare = 0, maxLong = 0;
            for (int s = 0; s < cases[c].seeds; s++)
            {
                FtRes o = falseTriggerRun(cases[c].sigma, cases[c].spdSd, dts[k], 1000u + 7919u * (uint32_t)s);
                sum += o.nMoving;
                if (o.nMoving > maxN) maxN = o.nMoving;
                if (o.movingShare > maxShare) maxShare = o.movingShare;
                if (o.longestS > maxLong) maxLong = o.longestS;
            }
            printf("T5 dt=%u sigma %.0f m speed sd %.1f: still -> moving %d over %d days (max %d/day), "
                   "max moving share %.4f, longest episode %.0f s\n",
                   (unsigned)dts[k], cases[c].sigma, cases[c].spdSd, sum, cases[c].seeds, maxN, maxShare, maxLong);
            TEST_ASSERT_TRUE_MESSAGE(maxN <= cases[c].maxPerDay, "too many false still -> moving per 24 h");
            TEST_ASSERT_TRUE_MESSAGE(maxShare < 0.01, "a stationary node spent >= 1 % of the day moving");
            TEST_ASSERT_TRUE_MESSAGE(maxLong < 600.0, "a false moving episode lasted >= 10 min");
        }
}

// T7: a standing node with a speed-noise burst (0.6 m/s, the node does not
// move) is declared moving after 10 s of evidence; the sample at that moment
// is a 150 m outlier. The catch-up gate must not let it pull the output: it is
// bounded by the distance the reported speed allows (advisor replay: 88 m
// excursion with the raw innovation alone, 10 m bounded).
static double outlierAtTriggerRun(uint32_t dt, uint32_t seed)
{
    struct Rng r; rngInit(&r, seed);
    struct PosFilter f; posFilterReset(&f);
    const double a = exp(-(double)dt / 60000.0), sd = 5.0 * sqrt(1.0 - a * a);
    double cx = rngGauss(&r, 5.0), cy = rngGauss(&r, 5.0), dmax = 0, tdet = -1;

    for (uint32_t t = 0; t <= 1200000u; t += dt)
    {
        double ts = t / 1000.0;
        cx = a * cx + rngGauss(&r, sd);
        cy = a * cy + rngGauss(&r, sd);
        double mx = cx + rngGauss(&r, 2.0), my = cy + rngGauss(&r, 2.0);
        float sp = (ts >= 600 && tdet < 0) ? 0.6f : 0.1f;
        uint32_t ds = dt > POS_KF_MOVE_SPEED_DT_MAX_MS ? POS_KF_MOVE_SPEED_DT_MAX_MS : dt;
        if (ts >= 600 && tdet < 0 && f.move_ms + ds >= POS_KF_MOVE_SPEED_MS)
            mx += 150.0;   // the outlier lands on the trigger sample
        upd(&f, mx, my, 1.0f, true, sp, 90.0f, POS_KF_MODE_FIXED, t == 0 ? 0 : dt);
        if (tdet < 0 && f.n_moving > 0) tdet = ts;
        if (tdet >= 0)
        {
            double fx, fy;
            outM(&f, &fx, &fy);
            double d = hypot(fx, fy);
            if (d > dmax) dmax = d;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(tdet >= 0, "the speed burst was not declared moving");
    return dmax;
}

void test_fixed_ausreisser_beim_moving(void)
{
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
    {
        double sum = 0;
        for (int s = 0; s < 10; s++)
            sum += outlierAtTriggerRun(dts[k], 77u + 31u * (uint32_t)s);
        printf("T7 dt=%u: 150 m outlier at the moving trigger, mean output excursion %.1f m\n",
               (unsigned)dts[k], sum / 10);
        TEST_ASSERT_TRUE_MESSAGE(sum / 10 < 25.0, "an outlier at the trigger sample pulled the output");
    }
}

// T8: cold start at a bad site. The adaptive radius must not start at its
// 40 m floor for the first half hour (advisor replay: 6-9 false triggers per
// 20 boots at sigma 15 m before the bias correction).
void test_fixed_kaltstart_keine_bewegung(void)
{
    uint32_t dts[2] = { DT1, DT3 };
    for (int k = 0; k < 2; k++)
    {
        int sum = 0;
        for (int s = 0; s < 20; s++)
        {
            struct Rng r; rngInit(&r, 1000u + 7919u * (uint32_t)s);
            struct PosFilter f; posFilterReset(&f);
            const double a = exp(-(double)dts[k] / 90000.0), sd = 15.0 * sqrt(1.0 - a * a);
            double cx = rngGauss(&r, 15.0), cy = rngGauss(&r, 15.0);
            for (uint32_t t = 0; t <= 1800000u; t += dts[k])
            {
                cx = a * cx + rngGauss(&r, sd);
                cy = a * cy + rngGauss(&r, sd);
                upd(&f, cx + rngGauss(&r, 2.0), cy + rngGauss(&r, 2.0), 1.0f, true, 0.0f, 0,
                    POS_KF_MODE_FIXED, t == 0 ? 0 : dts[k]);
            }
            sum += f.n_moving;
        }
        printf("T8 dt=%u: still -> moving in the first 30 min after 20 cold starts at sigma 15 m: %d\n",
               (unsigned)dts[k], sum);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, sum, "false moving right after a cold start");
    }
}

// T6: one outlier after a long FIXED gap must not re-seed. The reject timer
// counted the whole gap (120 s >= POS_KF_RESEED_MS), so an 80 m sample after
// the gap re-seeded onto itself: an 80 m output step, and the next true sample
// was rejected as an outlier.
void test_fixed_ausreisser_nach_luecke(void)
{
    struct Rng r; rngInit(&r, 606u);
    struct PosFilter f; posFilterReset(&f);
    for (int t = 0; t < 600; t++)
        upd(&f, rngGauss(&r, 3.0), rngGauss(&r, 3.0), 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED,
            t == 0 ? 0 : DT1);

    double bx, by, ax, ay;
    outM(&f, &bx, &by);
    enum PosFilterResult res = upd(&f, 80.0, 0.0, 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED, 120000u);
    outM(&f, &ax, &ay);
    printf("outlier after a 120 s FIXED gap: result %d, output step %.3f m\n", (int)res, hypot(ax - bx, ay - by));
    TEST_ASSERT_EQUAL_INT_MESSAGE(POS_KF_REJECT, res, "one outlier after a long gap must be rejected");
    TEST_ASSERT_TRUE_MESSAGE(hypot(ax - bx, ay - by) < 0.5, "output moved on the outlier");

    res = upd(&f, rngGauss(&r, 3.0), rngGauss(&r, 3.0), 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED, DT1);
    double cx, cy;
    outM(&f, &cx, &cy);
    printf("next true sample: result %d, output step %.3f m\n", (int)res, hypot(cx - ax, cy - ay));
    TEST_ASSERT_EQUAL_INT_MESSAGE(POS_KF_OK, res, "the next true sample must be accepted");
    TEST_ASSERT_TRUE_MESSAGE(hypot(cx - ax, cy - ay) < 0.5, "output step >= 0.5 m after the outlier");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, f.n_moving, "one outlier must not start the motion detection");
    TEST_ASSERT_TRUE(posFilterIsStill(&f));
}

// Sub-state contract: posFilterIsStill() only for a FIXED request while still;
// a TRACK request, a re-seed and a long gap while moving behave as documented.
void test_fixed_teilzustand_vertrag(void)
{
    struct PosFilter f; posFilterReset(&f);
    TEST_ASSERT_FALSE_MESSAGE(posFilterIsStill(&f), "no seed yet");
    TEST_ASSERT_EQUAL_UINT16(0, f.n_moving);
    TEST_ASSERT_EQUAL_UINT16(0, f.n_still);

    upd(&f, 0, 0, 1.0f, true, 0.0f, 0, POS_KF_MODE_FIXED, 0);
    TEST_ASSERT_TRUE_MESSAGE(posFilterIsStill(&f), "a FIXED seed starts still");

    // 15 s at 3 m/s east: speed criterion, moving after 10 s of net evidence
    double x = 0;
    int firstMoving = -1;
    for (int t = 1; t <= 15; t++)
    {
        x += 3.0;
        upd(&f, x, 0, 1.0f, true, 3.0f, 90.0f, POS_KF_MODE_FIXED, DT1);
        if (firstMoving < 0 && !posFilterIsStill(&f)) firstMoving = t;
    }
    printf("speed criterion: moving after %d s\n", firstMoving);
    TEST_ASSERT_TRUE_MESSAGE(firstMoving >= 9 && firstMoving <= 11, "moving must follow 10 s of speed evidence");
    TEST_ASSERT_EQUAL_UINT16(1, f.n_moving);
    TEST_ASSERT_EQUAL_INT(POS_KF_MODE_TRACK, f.mode);   // the model; the request is FIXED
    TEST_ASSERT_EQUAL_INT(POS_KF_MODE_FIXED, f.req);

    // a TRACK request: never "still", the transition counter does not move
    x += 3.0;
    upd(&f, x, 0, 1.0f, true, 3.0f, 90.0f, POS_KF_MODE_TRACK, DT1);
    TEST_ASSERT_FALSE(posFilterIsStill(&f));
    TEST_ASSERT_EQUAL_UINT16(1, f.n_moving);

    // back to FIXED while the CV velocity is still 3 m/s: stays moving, no event
    x += 3.0;
    upd(&f, x, 0, 1.0f, true, 3.0f, 90.0f, POS_KF_MODE_FIXED, DT1);
    TEST_ASSERT_FALSE_MESSAGE(posFilterIsStill(&f), "TRACK -> FIXED while moving must stay moving");
    TEST_ASSERT_EQUAL_UINT16(1, f.n_moving);

    // a gap while moving re-seeds, and the re-seed starts still
    enum PosFilterResult res = upd(&f, x + 500.0, 0, 1.0f, true, 3.0f, 90.0f, POS_KF_MODE_FIXED, 40000u);
    TEST_ASSERT_EQUAL_INT(POS_KF_RESEED, res);
    TEST_ASSERT_TRUE(posFilterIsStill(&f));

    // the counters survive a re-seed and clear on posFilterReset()
    TEST_ASSERT_EQUAL_UINT16(1, f.n_moving);
    posFilterReset(&f);
    TEST_ASSERT_EQUAL_UINT16(0, f.n_moving);
}

// A car with `--track off` crosses the 5 km re-anchor distance: moving all the
// way, no re-seed, no jump at the re-anchor (the still anchor moves with it).
void test_fixed_fahrzeug_reanchor(void)
{
    struct Rng r; rngInit(&r, 321u);
    struct PosFilter f; posFilterReset(&f);
    double x = 0, px = 0, py = 0, maxStep = 0, maxCatch = 0, lastErr = 0;
    int reseeds = 0;

    for (int t = 0; t <= 900; t++)
    {
        x += 20.0;   // 72 km/h east, 18 km in 15 min
        enum PosFilterResult res = upd(&f, x + rngGauss(&r, 4.0), rngGauss(&r, 4.0), 1.0f, true,
                                       (float)(20.0 + rngGauss(&r, 0.1)), 90.0f, POS_KF_MODE_FIXED,
                                       t == 0 ? 0 : DT1);
        if (t > 0 && res == POS_KF_RESEED) reseeds++;
        double fx, fy;
        outM(&f, &fx, &fy);
        double st = hypot(fx - px - 20.0, fy - py);   // deviation from the true motion
        // up to 40 s: 10 s of speed evidence (the car is 200 m away by then),
        // then the catch-up; afterwards the steady drive and the re-anchors
        if (t > 40) { if (st > maxStep) maxStep = st; }
        else if (t > 0 && st > maxCatch) maxCatch = st;
        px = fx; py = fy;
        lastErr = fx - x;
    }
    printf("FIXED car 18 km: reseeds %d, catch-up max step deviation %.1f m, then %.1f m, final error %.1f m, still transitions %d\n",
           reseeds, maxCatch, maxStep, lastErr, f.n_still);
    TEST_ASSERT_TRUE_MESSAGE(maxCatch < 60.0, "catch-up after the speed evidence jumped >= 60 m in one step");
    TEST_ASSERT_EQUAL_INT(0, reseeds);
    TEST_ASSERT_EQUAL_UINT16(0, f.n_still);
    TEST_ASSERT_TRUE_MESSAGE(maxStep < 10.0, "output jumped while driving or at the re-anchor");
    TEST_ASSERT_TRUE_MESSAGE(fabs(lastErr) < 15.0, "lost the vehicle");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_seed_liefert_erste_probe_unveraendert);
    RUN_TEST(test_seed_mit_hdop);
    RUN_TEST(test_stationaer_rauschen_viertel);
    RUN_TEST(test_hdop_gewichtung);
    RUN_TEST(test_hdop_floor_und_ungueltig);
    RUN_TEST(test_gate_und_zeitbasiertes_reseed);
    RUN_TEST(test_konstante_geschwindigkeit);
    RUN_TEST(test_kurve_ueberschwingen);
    RUN_TEST(test_kadenz_unabhaengig);
    RUN_TEST(test_dt_klemmung);
    RUN_TEST(test_reanchor_ohne_sprung);
    RUN_TEST(test_hemisphaeren_und_antimeridian);
    RUN_TEST(test_zero_velocity_update);
    RUN_TEST(test_sprung_15m_ohne_reseed);
    RUN_TEST(test_reject_timer_wird_bei_accept_zurueckgesetzt);
    RUN_TEST(test_fixed_korreliertes_rauschen);
    RUN_TEST(test_fixed_kadenz_teilstichprobe);
    RUN_TEST(test_fixed_luecke_ohne_reseed);
    RUN_TEST(test_moduswechsel_stetig);
    RUN_TEST(test_vel_valid_zupt_ohne_kurs);
    RUN_TEST(test_fixed_gehen_wahrheitsgemaess);
    RUN_TEST(test_fixed_gehen_static_hold);
    RUN_TEST(test_fixed_hundespaziergang_bleibt_moving);
    RUN_TEST(test_fixed_stehen_zurueck_auf_still);
    RUN_TEST(test_fixed_keine_falschen_bewegungen);
    RUN_TEST(test_fixed_ausreisser_beim_moving);
    RUN_TEST(test_fixed_kaltstart_keine_bewegung);
    RUN_TEST(test_fixed_ausreisser_nach_luecke);
    RUN_TEST(test_fixed_teilzustand_vertrag);
    RUN_TEST(test_fixed_fahrzeug_reanchor);
    return UNITY_END();
}
