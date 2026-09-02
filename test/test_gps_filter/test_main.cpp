// Native Testsuite fuer den GPS-Altitude-Filter und das Plausibilitaets-Gate aus
// src/gps_filter.h / gps_filter.cpp (GPS-02 / GPS-03).
//
// Hintergrund: der Commit-Gate bei gps_functions.cpp:959 laesst eine gespleisste
// NMEA-Zeile durch, wenn sie zufaellig eine gueltige Pruefsumme traegt (Nullinsel,
// unmoegliches Kalenderdatum). node_alt wird bisher unbehandelt aus jedem einzelnen
// Fix uebernommen; das Rauschen der GPS-Hoehe liegt bei mehreren Metern RMS. Der
// hier getestete Filter ist ein skalarer Kalman-Filter mit konstantem R, einem
// Innovations-Gate (grosse Spruenge werden verworfen statt eingerechnet) und einer
// Reseed-Regel fuer echte Standortwechsel ohne TRACK-Modus.
//
//   pio test -e native -f test_gps_filter

#include <unity.h>

#include <math.h>
#include <stdlib.h>

#include <gps_filter.h>

#include <traces/gpsdebug_alt_series.h>

void setUp(void)    {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Deterministischer LCG-Rauschgenerator (kein <random>, damit der Testlauf
// reproduzierbar bleibt): klassische LCG-Konstanten fuer die Uniform-Stufe,
// Box-Muller fuer die Normalverteilung.
// ---------------------------------------------------------------------------

static uint32_t s_lcgState = 1;

static double lcgUniform(void)
{
    s_lcgState = s_lcgState * 1103515245u + 12345u;
    return ((double)((s_lcgState >> 8) & 0x7fffffu)) / (double)0x800000;
}

static double lcgGaussian(double mean, double stddev)
{
    double u1 = lcgUniform();
    double u2 = lcgUniform();

    if (u1 < 1e-12)
        u1 = 1e-12;

    double z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
    return mean + stddev * z0;
}

static int cmpFloat(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

static float medianOf(const float *src, int n)
{
    float *tmp = (float *)malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++)
        tmp[i] = src[i];
    qsort(tmp, (size_t)n, sizeof(float), cmpFloat);
    float m = (n % 2) ? tmp[n / 2] : (tmp[n / 2 - 1] + tmp[n / 2]) / 2.0f;
    free(tmp);
    return m;
}

// ---------------------------------------------------------------------------
// 1. gpsSamplePlausible
// ---------------------------------------------------------------------------

static void test_plausible_akzeptiert_echten_fix(void)
{
    // OE5HWN-14, 2026-09-01 (doc bug-GPS-uart-overflow-20260901.md)
    TEST_ASSERT_TRUE(gpsSamplePlausible(48.2479, 14.2577, 2026, 9, 1));
}

static void test_plausible_verwirft_nullinsel_und_kalender(void)
{
    // lon == 0.0 (der gespleisste Sample aus gpsdebug.txt, GPSDEBUG_CORRUPT_*)
    TEST_ASSERT_FALSE(gpsSamplePlausible(GPSDEBUG_CORRUPT_LAT, GPSDEBUG_CORRUPT_LON,
                                          GPSDEBUG_CORRUPT_YEAR, GPSDEBUG_CORRUPT_MONTH,
                                          GPSDEBUG_CORRUPT_DAY));

    // lat == 0.0
    TEST_ASSERT_FALSE(gpsSamplePlausible(0.0, 14.2577, 2026, 9, 1));

    // Monat 14
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, 2026, 14, 1));

    // Tag 0
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, 2026, 9, 0));

    // Jahr 2015 (vor der erlaubten Untergrenze)
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, 2015, 9, 1));

    // Breite ausserhalb +-90
    TEST_ASSERT_FALSE(gpsSamplePlausible(91.0, 14.2577, 2026, 9, 1));

    // Laenge ausserhalb +-180
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, -181.0, 2026, 9, 1));
}

// ---------------------------------------------------------------------------
// 2. Filter im eingeschwungenen Zustand: 1000 Samples 280 + N(0, 4)
// ---------------------------------------------------------------------------

static void test_steady_state_haelt_rauschen_klein(void)
{
    struct AltFilter f;
    altFilterReset(&f);

    s_lcgState = 1; // fester Seed fuer Reproduzierbarkeit

    for (int i = 0; i < 1000; i++)
    {
        float meas = (float)(280.0 + lcgGaussian(0.0, 4.0));
        TEST_ASSERT_TRUE(altFilterUpdate(&f, meas));
    }

    TEST_ASSERT_TRUE(fabsf(f.x - 280.0f) < 1.0f);
    TEST_ASSERT_TRUE(f.P < 1.5f);
}

// ---------------------------------------------------------------------------
// 3. Kaltstart: der erste Sample seedet exakt
// ---------------------------------------------------------------------------

static void test_kaltstart_seedet_exakt(void)
{
    struct AltFilter f;
    altFilterReset(&f);

    TEST_ASSERT_FALSE(f.init);

    bool ok = altFilterUpdate(&f, 273.4f);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(f.init);
    TEST_ASSERT_EQUAL_FLOAT(273.4f, f.x);
    TEST_ASSERT_EQUAL_FLOAT(ALT_KF_P0, f.P);
    TEST_ASSERT_EQUAL_UINT8(0, f.rejects);
}

// ---------------------------------------------------------------------------
// 4. doc S3.2 Sequenz nach einem konvergierten 280 m Zustand
// ---------------------------------------------------------------------------

static void test_doc_sequenz_bleibt_nah_am_konvergierten_wert(void)
{
    struct AltFilter f;

    // konvergierter Zustand (Fixpunkt der P-Rekursion bei Q=0.01, R=185)
    f.x       = 280.0f;
    f.P       = 1.3552f;
    f.rejects = 0;
    f.init    = true;

    static const float seq[] = {278.6f, 273.6f, 268.5f, 267.2f,
                                 263.4f, 261.8f, 261.2f, 257.7f};

    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++)
    {
        altFilterUpdate(&f, seq[i]);
        // doc: worst raw sample 25.4 -> 2.6 m gefiltert; das Gate hier verwirft
        // die Ausreisser komplett, die Toleranz bleibt bei +-3 m um 280.
        TEST_ASSERT_TRUE(fabsf(f.x - 280.0f) <= 3.0f);
    }
}

// ---------------------------------------------------------------------------
// 5. Gate: ein einzelner +25 m Ausreisser wird verworfen, Zustand unveraendert
// ---------------------------------------------------------------------------

static void test_einzelner_ausreisser_wird_verworfen(void)
{
    struct AltFilter f;
    f.x       = 280.0f;
    f.P       = 1.3552f;
    f.rejects = 0;
    f.init    = true;

    float xBefore = f.x;
    float pBefore = f.P;

    bool ok = altFilterUpdate(&f, 305.0f); // 280 + 25

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_FLOAT(xBefore, f.x);
    TEST_ASSERT_EQUAL_FLOAT(pBefore, f.P);
    TEST_ASSERT_EQUAL_UINT8(1, f.rejects);
}

// ---------------------------------------------------------------------------
// 6. Re-Seed: 10 aufeinanderfolgende +50 m Samples seeden neu
// ---------------------------------------------------------------------------

static void test_zehn_ausreisser_seeden_neu(void)
{
    struct AltFilter f;
    f.x       = 280.0f;
    f.P       = 1.3552f;
    f.rejects = 0;
    f.init    = true;

    const float meas = 330.0f; // 280 + 50

    for (int i = 0; i < ALT_KF_RESEED_N - 1; i++)
    {
        bool ok = altFilterUpdate(&f, meas);
        TEST_ASSERT_FALSE_MESSAGE(ok, "reject count below the reseed threshold must stay rejected");
    }

    // der zehnte Ausreisser in Folge loest das Reseed aus
    bool ok = altFilterUpdate(&f, meas);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_FLOAT(meas, f.x);
    TEST_ASSERT_EQUAL_FLOAT(ALT_KF_P0, f.P);
    TEST_ASSERT_EQUAL_UINT8(0, f.rejects);
}

// ---------------------------------------------------------------------------
// 7. Konvergenz-Flag: false beim Seed, true nach <= 100 akzeptierten Samples
// ---------------------------------------------------------------------------

static void test_konvergenz_flag_kippt_innerhalb_100_samples(void)
{
    struct AltFilter f;
    altFilterReset(&f);

    altFilterSeed(&f, 275.0f);
    TEST_ASSERT_FALSE(altFilterConverged(&f));

    bool convergedWithin100 = false;

    for (int i = 0; i < 100; i++)
    {
        // ruhiges Signal, damit jeder Sample akzeptiert wird (das Gate ist nicht
        // Gegenstand dieses Tests)
        altFilterUpdate(&f, 275.0f + (float)(i % 3) - 1.0f);
        if (altFilterConverged(&f))
        {
            convergedWithin100 = true;
            break;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(convergedWithin100, "P did not fall below ALT_KF_P_CONV within 100 accepted samples");
}

// ---------------------------------------------------------------------------
// 8. Vollstaendige Feldserie aus dem Fixture (gpsdebug.txt, 381 Samples)
// ---------------------------------------------------------------------------
//
// Gemessen mit exakt diesem Filter (Q=0.01, R=185, P0=400, Gate=15, Reseed=10)
// gegen den Session-Median der Rohwerte: raw RMS 4.36 m, gefiltert 2.16 m. Der
// Rohwert deckt sich mit dem Doc-Wert (bug-GPS-uart-overflow-20260901.md S7.6:
// "4.36 -> 1.52 m"); das dortige 1.52 m ist eine EMA mit tau=300s ueber die
// bereits eingeschwungene Phase, nicht dieses Gate+Reseed-Filter ueber die volle
// Serie inklusive Kaltstart. Schwellen unten sind daher die gemessenen Werte
// dieses Filters (mit kleiner Toleranz fuer plattformabhaengige Fliesskomma-
// Rundung), nicht der Doc-Zielwert -- siehe Wave-1-A Bericht.
static void test_feldserie_rms_verbessert_sich(void)
{
    const int n = (int)(sizeof(GPSDEBUG_ALT) / sizeof(GPSDEBUG_ALT[0]));

    float median = medianOf(GPSDEBUG_ALT, n);

    struct AltFilter f;
    altFilterReset(&f);

    double sumSqRaw  = 0.0;
    double sumSqFilt = 0.0;
    double sumSqConv = 0.0;   // filtered, converged phase only (sample >= kConvStart)
    const int kConvStart = 100;

    for (int i = 0; i < n; i++)
    {
        altFilterUpdate(&f, GPSDEBUG_ALT[i]);

        double dRaw = (double)GPSDEBUG_ALT[i] - (double)median;
        sumSqRaw += dRaw * dRaw;

        double dFilt = (double)f.x - (double)median;
        sumSqFilt += dFilt * dFilt;
        if (i >= kConvStart)
            sumSqConv += dFilt * dFilt;
    }

    double rawRms  = sqrt(sumSqRaw / n);
    double filtRms = sqrt(sumSqFilt / n);
    double convRms = sqrt(sumSqConv / (n - kConvStart));

    // Whole series (cold start included) measured 2.16 m; the converged phase
    // alone measured 1.50 m, which is the doc's 1.52 m figure. Raw is 4.36 m.
    TEST_ASSERT_TRUE_MESSAGE(rawRms >= 3.5, "raw RMS dropped below the measured baseline (4.36 m)");
    TEST_ASSERT_TRUE_MESSAGE(filtRms <= 2.2, "filtered RMS regressed past the measured baseline (2.16 m)");
    TEST_ASSERT_TRUE_MESSAGE(convRms <= 1.6, "converged-phase RMS regressed past the measured baseline (1.50 m)");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_plausible_akzeptiert_echten_fix);
    RUN_TEST(test_plausible_verwirft_nullinsel_und_kalender);
    RUN_TEST(test_steady_state_haelt_rauschen_klein);
    RUN_TEST(test_kaltstart_seedet_exakt);
    RUN_TEST(test_doc_sequenz_bleibt_nah_am_konvergierten_wert);
    RUN_TEST(test_einzelner_ausreisser_wird_verworfen);
    RUN_TEST(test_zehn_ausreisser_seeden_neu);
    RUN_TEST(test_konvergenz_flag_kippt_innerhalb_100_samples);
    RUN_TEST(test_feldserie_rms_verbessert_sich);
    return UNITY_END();
}
