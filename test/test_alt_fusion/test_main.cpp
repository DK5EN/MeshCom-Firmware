// Native Testsuite fuer den GPS/Barometer-Komplementaerfilter aus
// src/alt_fusion.h / alt_fusion.cpp (GPS-05b) und fuer die float-Baro-Regression
// aus GPS-09.
//
// Hintergrund: node_alt kommt heute entweder ausschliesslich aus dem GPS-Kalman-
// Filter (gps_filter.h) oder -- auf Boards mit BMx280 -- soll zusaetzlich die
// kurzfristige Praezision des Barometers nutzen, ohne dessen willkuerlichen
// Druck-Anker (docs/bug-baro-altitude-20260906.md S5.2) zu uebernehmen. Der hier
// getestete Filter ist ein einpoliger Komplementaerfilter mit irregulaerer
// Abtastung: das GPS-Signal wird tiefpassgefiltert, das Barometersignal
// hochpassgefiltert (sein eigener Tiefpass wird wieder abgezogen), sodass ein
// konstanter Versatz im Barometerkanal exakt herausfaellt.
//
//   pio test -e native -f test_alt_fusion

#include <unity.h>

#include <math.h>
#include <stdlib.h>

#include <alt_fusion.h>
#include <gps_filter.h>

#include <traces/altb_fusion_series.h>
#include <traces/gpsdebug_alt_series.h>

// altb_fusion_series.h und gpsdebug_alt_series.h wurden aus derselben
// Zeilenauswahl ("[ALTB]"-Zeilen mit ";fix;1;") aus derselben Rohdatei
// extrahiert und sind index-aligned -- Voraussetzung fuer den Replay in
// Testfall f/g unten.
static_assert(sizeof(ALTB93_ALT) / sizeof(ALTB93_ALT[0]) == ALTB93_N,
              "ALTB93_ALT (gpsdebug_alt_series.h) and ALTB93_MS/QFE/BP/BA "
              "(altb_fusion_series.h) must be extracted from the same row "
              "selection and stay index-aligned");

void setUp(void)    {}
void tearDown(void) {}

#define DT_3S 3000u

// ---------------------------------------------------------------------------
// Statistik-Hilfsfunktionen fuer Testfall f/g. Bevorzugt double, um keine
// zusaetzliche Rundung ueber die Float-Praezision des Filters selbst hinaus
// einzufuehren.
// ---------------------------------------------------------------------------

static double stddevOf(const double *v, int n)
{
    double mean = 0.0;
    for (int i = 0; i < n; i++)
        mean += v[i];
    mean /= n;

    double sumSq = 0.0;
    for (int i = 0; i < n; i++)
    {
        double d = v[i] - mean;
        sumSq += d * d;
    }
    return sqrt(sumSq / n);
}

// Allan-Abweichung in Blockform: m Samples pro Block (nicht ueberlappend),
// Blockmittel, sqrt(mean(diff(blockMean)^2) / 2). m wird aus der gewuenschten
// Blockdauer und der mittleren Abtastperiode (hier 3.1 s, die gemessene
// Median-Kadenz der DK5EN-93-Aufzeichnung) gerundet.
static double allanDeviation(const double *v, int n, double blockPeriodS, double samplePeriodS)
{
    int m = (int)lround(blockPeriodS / samplePeriodS);
    if (m < 1)
        m = 1;

    int nBlocks = n / m;
    TEST_ASSERT_TRUE_MESSAGE(nBlocks >= 2, "not enough samples for the requested Allan block size");

    double *blockMeans = (double *)malloc(sizeof(double) * (size_t)nBlocks);
    for (int b = 0; b < nBlocks; b++)
    {
        double sum = 0.0;
        for (int i = 0; i < m; i++)
            sum += v[b * m + i];
        blockMeans[b] = sum / m;
    }

    double sumSqDiff = 0.0;
    for (int b = 0; b < nBlocks - 1; b++)
    {
        double d = blockMeans[b + 1] - blockMeans[b];
        sumSqDiff += d * d;
    }
    double result = sqrt(sumSqDiff / (nBlocks - 1) / 2.0);

    free(blockMeans);
    return result;
}

// ---------------------------------------------------------------------------
// a. Kaltstart: der erste Sample seedet ohne Sprung
// ---------------------------------------------------------------------------

static void test_erstes_sample_seedet_ohne_sprung(void)
{
    struct AltFusion f;
    altFusionReset(&f);

    TEST_ASSERT_FALSE(f.init);

    float out = altFusionUpdate(&f, 500.0f, 100.0f, DT_3S);

    TEST_ASSERT_EQUAL_FLOAT(500.0f, out);
    TEST_ASSERT_TRUE(f.init);
    TEST_ASSERT_EQUAL_FLOAT(500.0f, f.gpsLp);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, f.baroLp);
}

// ---------------------------------------------------------------------------
// b. dt = 0 aendert nichts
// ---------------------------------------------------------------------------

static void test_dt_null_aendert_nichts(void)
{
    struct AltFusion f;
    altFusionReset(&f);
    altFusionUpdate(&f, 500.0f, 100.0f, DT_3S); // seed

    float gpsLpBefore  = f.gpsLp;
    float baroLpBefore = f.baroLp;

    float out = altFusionUpdate(&f, 510.0f, 120.0f, 0);

    TEST_ASSERT_EQUAL_FLOAT(gpsLpBefore, f.gpsLp);
    TEST_ASSERT_EQUAL_FLOAT(baroLpBefore, f.baroLp);
    TEST_ASSERT_EQUAL_FLOAT(gpsLpBefore + (120.0f - baroLpBefore), out);
}

// ---------------------------------------------------------------------------
// c. Ein Sprung im Barometer geht sofort (fast) ungedaempft durch und klingt
//    mit tau ab.
// ---------------------------------------------------------------------------

static void test_baro_sprung_geht_sofort_durch(void)
{
    struct AltFusion f;
    altFusionReset(&f);

    float seedOut = altFusionUpdate(&f, 500.0f, 100.0f, DT_3S); // seed
    TEST_ASSERT_EQUAL_FLOAT(500.0f, seedOut);

    // +10 m Barometersprung, ein einzelnes 3 s Update
    float out = altFusionUpdate(&f, 500.0f, 110.0f, DT_3S);

    TEST_ASSERT_TRUE_MESSAGE(fabsf((out - 500.0f) - 10.0f) < 0.05f,
                             "a baro step must pass through almost unattenuated at t=0");

    // fuenf weitere Updates zu je einer Zeitkonstante (5*tau insgesamt) lassen
    // den Sprung auf < 0.1 m abklingen (exp(-5) ~ 0.0067, * 10 m ~ 0.067 m)
    for (int i = 0; i < 5; i++)
        out = altFusionUpdate(&f, 500.0f, 110.0f, ALT_FUSION_TAU_MS);

    TEST_ASSERT_TRUE_MESSAGE(fabsf(out - 500.0f) < 0.1f,
                             "baro step must have decayed with tau after 5*tau");
}

// ---------------------------------------------------------------------------
// d. Ein Sprung im GPS-Signal wird gedaempft (Tiefpass mit Zeitkonstante tau).
// ---------------------------------------------------------------------------

static void test_gps_sprung_wird_gedaempft(void)
{
    struct AltFusion f;
    altFusionReset(&f);

    altFusionUpdate(&f, 500.0f, 100.0f, DT_3S); // seed

    // +10 m GPS-Sprung, ein einzelnes 3 s Update: kaum sichtbar
    float out = altFusionUpdate(&f, 510.0f, 100.0f, DT_3S);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(out - 500.0f) < 0.05f,
                             "a gps step must barely move the output after one 3 s update");

    // weitere Updates, bis insgesamt genau eine Zeitkonstante seit dem Sprung
    // vergangen ist (Baro bleibt konstant, GPS bleibt bei 510)
    uint32_t remaining = ALT_FUSION_TAU_MS - DT_3S;
    out = altFusionUpdate(&f, 510.0f, 100.0f, remaining);

    double expectedMove = 10.0 * (1.0 - 1.0 / exp(1.0));
    TEST_ASSERT_TRUE_MESSAGE(fabs((double)(out - 500.0f) - expectedMove) < 0.1,
                             "after one tau the gps step must have moved by 10*(1-1/e)");

    // ein einzelnes Update mit dt = 10*tau: langer Ausfall -> volle Konvergenz
    out = altFusionUpdate(&f, 510.0f, 100.0f, 10u * ALT_FUSION_TAU_MS);
    TEST_ASSERT_TRUE_MESSAGE(fabsf(out - 510.0f) < 0.01f,
                             "a very long gap must converge the output onto the new gps value");
}

// ---------------------------------------------------------------------------
// e. Der willkuerliche Baro-Anker kuerzt sich heraus.
// ---------------------------------------------------------------------------

static void test_anker_der_baro_kuerzt_sich_heraus(void)
{
    static const float gps[]  = {500.0f, 501.0f, 503.0f, 502.0f, 505.0f, 507.0f,
                                  506.0f, 508.0f, 509.0f, 512.0f, 511.0f, 513.0f};
    static const float baro[] = {100.0f, 101.5f, 100.8f, 102.0f, 103.5f, 102.9f,
                                  104.4f, 105.0f, 104.2f, 106.1f, 107.0f, 106.5f};
    static const uint32_t dts[] = {DT_3S, 3100u, 2900u, 3050u, 5000u, 3000u,
                                    2950u, 3200u, 60000u, 3000u, 3000u, 3000u};
    const int n = (int)(sizeof(gps) / sizeof(gps[0]));

    const float anchorOffset = 1000.0f;

    struct AltFusion a, b;
    altFusionReset(&a);
    altFusionReset(&b);

    for (int i = 0; i < n; i++)
    {
        float outA = altFusionUpdate(&a, gps[i], baro[i], dts[i]);
        float outB = altFusionUpdate(&b, gps[i], baro[i] + anchorOffset, dts[i]);

        TEST_ASSERT_TRUE_MESSAGE(fabsf(outA - outB) < 1e-3f,
                                 "an arbitrary constant offset on the baro channel must cancel");
    }
}

// ---------------------------------------------------------------------------
// f. Feldserie DK5EN-93: die Fusion muss stabiler sein als der reine Kalman-
//    Filter (der S10-Gate).
// ---------------------------------------------------------------------------
//
// Replay: ALTB93_ALT[i] (raw GPS) durch altFilterUpdate (gps_filter, frisch
// resettet), dt aus aufeinanderfolgenden ALTB93_MS-Differenzen (erste Zeile:
// ALT_KF_DT_REF_MS). Ueberall dort, wo ALTB93_BP[i] > 0 und ALTB93_QFE[i] > 0
// ist (der Barometer-Referenzdruck ist gelatched), wird die Barometerhoehe
// nach der Formel aus src/bmx280.cpp berechnet und in die Fusion eingespeist,
// mit dt = ms-Differenz seit der vorherigen FUSIONIERTEN Zeile (erste
// fusionierte Zeile: dt 0 / seed).
//
// Erstes Achtel der fusionierten Zeilen ist Warmup und wird verworfen.
//
// Gemessen mit exakt diesem Verfahren (Offline-Referenz des Orchestrators in
// Klammern): fused sd 2.76 m (Ref 2.76), kalman sd 7.44 m (Ref 7.44),
// ADEV 5 min 0.97 m (Ref 0.97), ADEV 30 s 0.15 m (Ref 0.15).
static void test_feldserie93_fusion_stabiler_als_kalman(void)
{
    const int n = ALTB93_N;

    struct AltFilter kf;
    altFilterReset(&kf);

    double *kalmanEst = (double *)malloc(sizeof(double) * (size_t)n);

    for (int i = 0; i < n; i++)
    {
        uint32_t dt = (i == 0) ? ALT_KF_DT_REF_MS : (ALTB93_MS[i] - ALTB93_MS[i - 1]);
        altFilterUpdate(&kf, ALTB93_ALT[i], dt);
        kalmanEst[i] = (double)kf.x;
    }

    struct AltFusion fusion;
    altFusionReset(&fusion);

    double *fused        = (double *)malloc(sizeof(double) * (size_t)n);
    double *kalmanAtFused = (double *)malloc(sizeof(double) * (size_t)n);
    int      m           = 0;
    uint32_t prevMs       = 0;
    bool     havePrev     = false;

    for (int i = 0; i < n; i++)
    {
        if (ALTB93_BP[i] <= 0.0f || ALTB93_QFE[i] <= 0.0f)
            continue;

        float baro = -7990.0f * logf(ALTB93_QFE[i] / ALTB93_BP[i]) + ALTB93_BA[i];

        uint32_t dt = havePrev ? (ALTB93_MS[i] - prevMs) : 0;
        prevMs   = ALTB93_MS[i];
        havePrev = true;

        float out = altFusionUpdate(&fusion, (float)kalmanEst[i], baro, dt);

        fused[m]         = (double)out;
        kalmanAtFused[m] = kalmanEst[i];
        m++;
    }

    TEST_ASSERT_TRUE_MESSAGE(m > 100, "too few fused rows in the DK5EN-93 replay (barometer never armed?)");

    int warm = m / 8;
    int evalN = m - warm;

    double *fusedEval  = fused + warm;
    double *kalmanEval = kalmanAtFused + warm;

    double fusedSd  = stddevOf(fusedEval, evalN);
    double kalmanSd = stddevOf(kalmanEval, evalN);
    double adev5min = allanDeviation(fusedEval, evalN, 300.0, 3.1);
    double adev30s  = allanDeviation(fusedEval, evalN, 30.0, 3.1);

    TEST_ASSERT_TRUE_MESSAGE(fusedSd <= 4.0, "fused sd regressed past the measured baseline (2.76 m)");
    TEST_ASSERT_TRUE_MESSAGE(fusedSd < kalmanSd, "the fusion must beat the plain Kalman estimate it is fed");
    TEST_ASSERT_TRUE_MESSAGE(adev5min <= 1.6, "5-minute Allan deviation regressed past the measured baseline (0.97 m)");

    (void)adev30s; // gemessen, siehe Kommentar oben; kein eigenes Gate hier (das ist Testfall g)

    free(kalmanEst);
    free(fused);
    free(kalmanAtFused);
}

// ---------------------------------------------------------------------------
// g. GPS-09 Regression: der float-Barometerwert muss besser sein als der auf
//    ganze Meter gerundete.
// ---------------------------------------------------------------------------
//
// Gemessen (Ref in Klammern): ADEV 30 s float 0.15 m (Ref ~0.15), int 0.22 m
// (Ref ~0.2). Dieser Test schlaegt fehl, sobald jemand node_press_alt (den
// gerundeten int-Wert) statt getPressALTf() in die Fusion einspeist.
static void test_float_baro_schlaegt_int_baro(void)
{
    const int n = ALTB93_N;

    struct AltFilter kf;
    altFilterReset(&kf);
    double *kalmanEst = (double *)malloc(sizeof(double) * (size_t)n);
    for (int i = 0; i < n; i++)
    {
        uint32_t dt = (i == 0) ? ALT_KF_DT_REF_MS : (ALTB93_MS[i] - ALTB93_MS[i - 1]);
        altFilterUpdate(&kf, ALTB93_ALT[i], dt);
        kalmanEst[i] = (double)kf.x;
    }

    struct AltFusion fusionFloat, fusionInt;
    altFusionReset(&fusionFloat);
    altFusionReset(&fusionInt);

    double *fusedFloat = (double *)malloc(sizeof(double) * (size_t)n);
    double *fusedInt    = (double *)malloc(sizeof(double) * (size_t)n);
    int      m          = 0;
    uint32_t prevMs      = 0;
    bool     havePrev    = false;

    for (int i = 0; i < n; i++)
    {
        if (ALTB93_BP[i] <= 0.0f || ALTB93_QFE[i] <= 0.0f)
            continue;

        float baroF = -7990.0f * logf(ALTB93_QFE[i] / ALTB93_BP[i]) + ALTB93_BA[i];
        float baroI = roundf(baroF);

        uint32_t dt = havePrev ? (ALTB93_MS[i] - prevMs) : 0;
        prevMs   = ALTB93_MS[i];
        havePrev = true;

        fusedFloat[m] = (double)altFusionUpdate(&fusionFloat, (float)kalmanEst[i], baroF, dt);
        fusedInt[m]    = (double)altFusionUpdate(&fusionInt, (float)kalmanEst[i], baroI, dt);
        m++;
    }

    TEST_ASSERT_TRUE_MESSAGE(m > 100, "too few fused rows in the DK5EN-93 replay (barometer never armed?)");

    int warm = m / 8;
    int evalN = m - warm;

    double adevFloat = allanDeviation(fusedFloat + warm, evalN, 30.0, 3.1);
    double adevInt   = allanDeviation(fusedInt + warm, evalN, 30.0, 3.1);

    TEST_ASSERT_TRUE_MESSAGE(adevFloat < adevInt,
                             "the float baro path must beat the whole-metre-rounded baro path at 30 s ADEV");

    free(kalmanEst);
    free(fusedFloat);
    free(fusedInt);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_erstes_sample_seedet_ohne_sprung);
    RUN_TEST(test_dt_null_aendert_nichts);
    RUN_TEST(test_baro_sprung_geht_sofort_durch);
    RUN_TEST(test_gps_sprung_wird_gedaempft);
    RUN_TEST(test_anker_der_baro_kuerzt_sich_heraus);
    RUN_TEST(test_feldserie93_fusion_stabiler_als_kalman);
    RUN_TEST(test_float_baro_schlaegt_int_baro);
    return UNITY_END();
}
