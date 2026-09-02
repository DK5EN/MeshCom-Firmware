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

// Nennkadenz einer ESP32-Auswertung. Die Feldserien wurden mit genau dieser
// Kadenz aufgezeichnet, also ist sie auch der dt-Wert der Serientests.
#define DT_3S 3000u

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

// Laesst den Filter ueber eine ganze Feldserie laufen und liefert drei
// RMS-Werte um den Sessionsmedian: roh, gefiltert ueber die ganze Serie
// (Kaltstart eingeschlossen) und gefiltert ab kConvStart.
static void seriesRms(const float *series, int n, int kConvStart,
                      double *rawRms, double *filtRms, double *convRms)
{
    float median = medianOf(series, n);

    struct AltFilter f;
    altFilterReset(&f);

    double sumSqRaw  = 0.0;
    double sumSqFilt = 0.0;
    double sumSqConv = 0.0;

    for (int i = 0; i < n; i++)
    {
        altFilterUpdate(&f, series[i], DT_3S);

        double dRaw = (double)series[i] - (double)median;
        sumSqRaw += dRaw * dRaw;

        double dFilt = (double)f.x - (double)median;
        sumSqFilt += dFilt * dFilt;
        if (i >= kConvStart)
            sumSqConv += dFilt * dFilt;
    }

    *rawRms  = sqrt(sumSqRaw / n);
    *filtRms = sqrt(sumSqFilt / n);
    *convRms = sqrt(sumSqConv / (n - kConvStart));
}

// ---------------------------------------------------------------------------
// 1. gpsSamplePlausible / gpsDatePlausible / gpsTimePlausible
// ---------------------------------------------------------------------------

static void test_plausible_akzeptiert_echten_fix(void)
{
    // OE5HWN-14, 2026-09-01 (doc bug-GPS-uart-overflow-20260901.md)
    TEST_ASSERT_TRUE(gpsSamplePlausible(48.2479, 14.2577, 280.0, 2026, 9, 1));
}

static void test_plausible_verwirft_nullinsel_und_kalender(void)
{
    // lon == 0.0 (der gespleisste Sample aus gpsdebug.txt, GPSDEBUG_CORRUPT_*)
    TEST_ASSERT_FALSE(gpsSamplePlausible(GPSDEBUG_CORRUPT_LAT, GPSDEBUG_CORRUPT_LON, 280.0,
                                          GPSDEBUG_CORRUPT_YEAR, GPSDEBUG_CORRUPT_MONTH,
                                          GPSDEBUG_CORRUPT_DAY));

    // lat == 0.0
    TEST_ASSERT_FALSE(gpsSamplePlausible(0.0, 14.2577, 280.0, 2026, 9, 1));

    // Monat 14
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, 280.0, 2026, 14, 1));

    // Tag 0
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, 280.0, 2026, 9, 0));

    // Jahr 2015 (vor der erlaubten Untergrenze)
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, 280.0, 2015, 9, 1));

    // Breite ausserhalb +-90
    TEST_ASSERT_FALSE(gpsSamplePlausible(91.0, 14.2577, 280.0, 2026, 9, 1));

    // Laenge ausserhalb +-180
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, -181.0, 280.0, 2026, 9, 1));
}

// F5: eine Muellhoehe darf node_alt nicht seeden (der Filter heilt sich zwar in
// zehn Stichproben, aber der erste Wert geht sofort in die Konfiguration).
static void test_plausible_verwirft_hoehen_ausserhalb_des_bereichs(void)
{
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, -501.0, 2026, 9, 1));
    TEST_ASSERT_FALSE(gpsSamplePlausible(48.2479, 14.2577, 10001.0, 2026, 9, 1));

    // Die Raender selbst bleiben gueltig (Totes Meer / Hoehenballon-Grenzfall).
    TEST_ASSERT_TRUE(gpsSamplePlausible(48.2479, 14.2577, GPS_ALT_MIN_M, 2026, 9, 1));
    TEST_ASSERT_TRUE(gpsSamplePlausible(48.2479, 14.2577, GPS_ALT_MAX_M, 2026, 9, 1));
    TEST_ASSERT_TRUE(gpsSamplePlausible(48.2479, 14.2577, 0.0, 2026, 9, 1));
}

static void test_datum_plausibilitaet(void)
{
    TEST_ASSERT_TRUE(gpsDatePlausible(2026, 9, 1));
    TEST_ASSERT_TRUE(gpsDatePlausible(2024, 1, 1));
    TEST_ASSERT_TRUE(gpsDatePlausible(2099, 12, 31));

    TEST_ASSERT_FALSE(gpsDatePlausible(2023, 12, 31));
    TEST_ASSERT_FALSE(gpsDatePlausible(2100, 1, 1));
    TEST_ASSERT_FALSE(gpsDatePlausible(2026, 0, 1));
    TEST_ASSERT_FALSE(gpsDatePlausible(2026, 13, 1));
    TEST_ASSERT_FALSE(gpsDatePlausible(2026, 9, 0));
    TEST_ASSERT_FALSE(gpsDatePlausible(2026, 9, 32));
}

// F1: ein gespleisstes RMC kann den Datumsteil heil und den Zeitteil zerstoert
// liefern -- "1834" wird zu 00:18:34, "999999.99" zu h=99.
static void test_zeit_plausibilitaet(void)
{
    TEST_ASSERT_TRUE(gpsTimePlausible(0, 0, 0));
    TEST_ASSERT_TRUE(gpsTimePlausible(18, 28, 3));
    TEST_ASSERT_TRUE(gpsTimePlausible(23, 59, 59));
    // Schaltsekunde
    TEST_ASSERT_TRUE(gpsTimePlausible(23, 59, 60));

    TEST_ASSERT_FALSE(gpsTimePlausible(24, 0, 0));
    TEST_ASSERT_FALSE(gpsTimePlausible(99, 99, 99));
    TEST_ASSERT_FALSE(gpsTimePlausible(0, 60, 0));
    TEST_ASSERT_FALSE(gpsTimePlausible(0, 0, 61));
    TEST_ASSERT_FALSE(gpsTimePlausible(-1, 0, 0));
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
        TEST_ASSERT_TRUE(altFilterUpdate(&f, meas, DT_3S));
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

    bool ok = altFilterUpdate(&f, 273.4f, DT_3S);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(f.init);
    TEST_ASSERT_EQUAL_FLOAT(273.4f, f.x);
    TEST_ASSERT_EQUAL_FLOAT(ALT_KF_P0, f.P);
    TEST_ASSERT_EQUAL_UINT8(0, f.rejects);
}

// ---------------------------------------------------------------------------
// 4. doc S3.2 Sequenz nach einem konvergierten 280 m Zustand
// ---------------------------------------------------------------------------
//
// F8: die alte Fassung dieses Falls hat nur eine +-3 m Schranke geprueft und
// ist auch mit geloeschtem Gate durchgelaufen (max. Abweichung 0.78 m). Hier
// steht jetzt die Gate-Entscheidung jeder einzelnen Stichprobe: die vier
// Werte, die mehr als ALT_KF_GATE_M unter dem Zustand liegen, muessen
// verworfen werden, und der Zustand darf sich ueber die ganze Sequenz um
// weniger als 0.5 m bewegen (gemessen 0.23 m; ohne Gate 0.78 m).
static void test_doc_sequenz_gate_verwirft_genau_vier_samples(void)
{
    struct AltFilter f;

    // konvergierter Zustand (Fixpunkt der P-Rekursion bei Q=0.01, R=185)
    f.x       = 280.0f;
    f.P       = 1.3552f;
    f.rejects = 0;
    f.init    = true;

    static const float seq[] = {278.6f, 273.6f, 268.5f, 267.2f,
                                 263.4f, 261.8f, 261.2f, 257.7f};
    // 263.4 und tiefer liegen mehr als 15 m unter dem (kaum bewegten) Zustand
    static const bool expectAccept[] = {true, true, true, true,
                                        false, false, false, false};

    const int n = (int)(sizeof(seq) / sizeof(seq[0]));
    int accepted = 0;
    int rejected = 0;

    for (int i = 0; i < n; i++)
    {
        bool ok = altFilterUpdate(&f, seq[i], DT_3S);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)expectAccept[i], (int)ok,
                                      "gate decision changed for a doc-sequence sample");
        if (ok)
            accepted++;
        else
            rejected++;
    }

    TEST_ASSERT_EQUAL_INT(4, accepted);
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, rejected, "the four samples >15 m below 280 must be rejected");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(4, f.rejects, "consecutive-reject counter must hold the run");
    TEST_ASSERT_TRUE_MESSAGE(fabsf(f.x - 280.0f) < 0.5f,
                             "state moved further than the gated 0.23 m (gate ineffective?)");
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

    bool ok = altFilterUpdate(&f, 305.0f, DT_3S); // 280 + 25

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
        bool ok = altFilterUpdate(&f, meas, DT_3S);
        TEST_ASSERT_FALSE_MESSAGE(ok, "reject count below the reseed threshold must stay rejected");
    }

    // der zehnte Ausreisser in Folge loest das Reseed aus
    bool ok = altFilterUpdate(&f, meas, DT_3S);
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
        altFilterUpdate(&f, 275.0f + (float)(i % 3) - 1.0f, DT_3S);
        if (altFilterConverged(&f))
        {
            convergedWithin100 = true;
            break;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(convergedWithin100, "P did not fall below ALT_KF_P_CONV within 100 accepted samples");
}

// ---------------------------------------------------------------------------
// 8. F2: dt_ms bestimmt das eingespeiste Prozessrauschen
// ---------------------------------------------------------------------------
//
// Die Auswertung laeuft auf nRF52 im Sekundentakt, auf ESP32 im
// Dreisekundentakt. Mit einem festen Q pro Aufruf haengt die Zeitkonstante des
// Schaetzers an der Kadenz (136 s vs 408 s); mit dt_ms nicht mehr.

static void test_drei_updates_bei_1s_wie_eines_bei_3s(void)
{
    struct AltFilter a;
    a.x       = 280.0f;
    a.P       = ALT_KF_P_CONV;
    a.rejects = 0;
    a.init    = true;

    struct AltFilter b = a;

    // gleiche Wanduhrzeit (3 s), einmal in drei Schritten, einmal in einem
    for (int i = 0; i < 3; i++)
        TEST_ASSERT_TRUE(altFilterUpdate(&a, 280.0f, 1000));

    TEST_ASSERT_TRUE(altFilterUpdate(&b, 280.0f, 3000));

    // gemessen 2.4121 (3x1s) gegen 2.4764 (1x3s) -> 2.6 %
    float rel = fabsf(a.P - b.P) / b.P;
    TEST_ASSERT_TRUE_MESSAGE(rel < 0.05f,
                             "P over one 3 s window from a converged state must not depend on the evaluation cadence (steady-state P does, by sqrt(dt))");
}

static void test_dt_skaliert_das_prozessrauschen(void)
{
    struct AltFilter one;
    one.x       = 280.0f;
    one.P       = ALT_KF_P_CONV;
    one.rejects = 0;
    one.init    = true;

    struct AltFilter three = one;

    // je EIN Update, also identische Messkorrektur: die ganze Differenz ist das
    // Prozessrauschen. Q * (1 - 1/3) = 0.006667 vor der Korrektur, gemessen
    // 0.006490 danach. Mit einem festen Q pro Aufruf waere die Differenz 0.
    TEST_ASSERT_TRUE(altFilterUpdate(&one,   280.0f, 1000));
    TEST_ASSERT_TRUE(altFilterUpdate(&three, 280.0f, 3000));

    float diff = three.P - one.P;
    TEST_ASSERT_TRUE_MESSAGE(diff > 0.005f && diff < 0.008f,
                             "dt_ms does not scale the injected process noise");
}

static void test_dt_wird_geklemmt(void)
{
    struct AltFilter base;
    base.x       = 280.0f;
    base.P       = ALT_KF_P_CONV;
    base.rejects = 0;
    base.init    = true;

    struct AltFilter cap  = base;
    struct AltFilter over = base;
    struct AltFilter zero = base;

    TEST_ASSERT_TRUE(altFilterUpdate(&cap,  280.0f, ALT_KF_DT_MAX_MS));
    TEST_ASSERT_TRUE(altFilterUpdate(&over, 280.0f, 10u * ALT_KF_DT_MAX_MS));
    TEST_ASSERT_TRUE(altFilterUpdate(&zero, 280.0f, 0));

    // ein sehr langer Ausfall darf P nicht sprengen
    TEST_ASSERT_EQUAL_FLOAT(cap.P, over.P);
    // dt = 0 speist kein Prozessrauschen ein
    TEST_ASSERT_TRUE(zero.P < cap.P);
    TEST_ASSERT_TRUE(zero.P < base.P);
}

// ---------------------------------------------------------------------------
// 9. Vollstaendige Feldserien aus dem Fixture
// ---------------------------------------------------------------------------
//
// Gemessen mit exakt diesem Filter (Q=0.01, R=185, P0=400, Gate=15, Reseed=10,
// dt=3000 ms) gegen den Session-Median der Rohwerte. Der Rohwert der ersten
// Serie deckt sich mit dem Doc-Wert (bug-GPS-uart-overflow-20260901.md S7.6:
// "4.36 -> 1.52 m"); das dortige 1.52 m ist eine EMA mit tau=300s ueber die
// bereits eingeschwungene Phase, nicht dieses Gate+Reseed-Filter ueber die
// volle Serie inklusive Kaltstart. Schwellen unten sind daher die gemessenen
// Werte dieses Filters (mit kleiner Toleranz fuer plattformabhaengige
// Fliesskomma-Rundung), nicht der Doc-Zielwert.

static const int kConvStart = 100;   // Kaltstart-Phase, gilt fuer beide Serien

static void test_feldserie_rms_verbessert_sich(void)
{
    const int n = (int)(sizeof(GPSDEBUG_ALT) / sizeof(GPSDEBUG_ALT[0]));

    double rawRms, filtRms, convRms;
    seriesRms(GPSDEBUG_ALT, n, kConvStart, &rawRms, &filtRms, &convRms);

    // gemessen: raw 4.364, ganze Serie 2.162, konvergierte Phase 1.498
    TEST_ASSERT_TRUE_MESSAGE(rawRms >= 3.5, "raw RMS dropped below the measured baseline (4.36 m)");
    TEST_ASSERT_TRUE_MESSAGE(filtRms <= 2.2, "filtered RMS regressed past the measured baseline (2.16 m)");
    TEST_ASSERT_TRUE_MESSAGE(convRms <= 1.6, "converged-phase RMS regressed past the measured baseline (1.50 m)");
}

// F8: die zweite Aufzeichnung (gpsdebug1.txt, 305 Samples, derselbe Knoten
// ~20 min spaeter) war bisher unbenutzt. Sie traegt eine deutlich groessere
// echte Drift (269..290 m), also ist die ganze Serie hier schlechter als in
// der ersten Aufzeichnung, die konvergierte Phase dagegen besser.
static void test_feldserie2_rms_verbessert_sich(void)
{
    const int n = (int)(sizeof(GPSDEBUG1_ALT) / sizeof(GPSDEBUG1_ALT[0]));

    double rawRms, filtRms, convRms;
    seriesRms(GPSDEBUG1_ALT, n, kConvStart, &rawRms, &filtRms, &convRms);

    // gemessen: raw 4.078, ganze Serie 3.271, konvergierte Phase 0.709
    TEST_ASSERT_TRUE_MESSAGE(rawRms >= 3.5, "raw RMS dropped below the measured baseline (4.08 m)");
    TEST_ASSERT_TRUE_MESSAGE(filtRms <= 3.4, "filtered RMS regressed past the measured baseline (3.27 m)");
    TEST_ASSERT_TRUE_MESSAGE(convRms <= 0.9, "converged-phase RMS regressed past the measured baseline (0.71 m)");
    TEST_ASSERT_TRUE_MESSAGE(convRms < rawRms, "the filter must beat the raw series it is fed");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_plausible_akzeptiert_echten_fix);
    RUN_TEST(test_plausible_verwirft_nullinsel_und_kalender);
    RUN_TEST(test_plausible_verwirft_hoehen_ausserhalb_des_bereichs);
    RUN_TEST(test_datum_plausibilitaet);
    RUN_TEST(test_zeit_plausibilitaet);
    RUN_TEST(test_steady_state_haelt_rauschen_klein);
    RUN_TEST(test_kaltstart_seedet_exakt);
    RUN_TEST(test_doc_sequenz_gate_verwirft_genau_vier_samples);
    RUN_TEST(test_einzelner_ausreisser_wird_verworfen);
    RUN_TEST(test_zehn_ausreisser_seeden_neu);
    RUN_TEST(test_konvergenz_flag_kippt_innerhalb_100_samples);
    RUN_TEST(test_drei_updates_bei_1s_wie_eines_bei_3s);
    RUN_TEST(test_dt_skaliert_das_prozessrauschen);
    RUN_TEST(test_dt_wird_geklemmt);
    RUN_TEST(test_feldserie_rms_verbessert_sich);
    RUN_TEST(test_feldserie2_rms_verbessert_sich);
    return UNITY_END();
}
