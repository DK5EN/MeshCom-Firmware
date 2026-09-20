// Native Testsuite fuer sanitize_radio_params() / sanitize_cstring() (TM-32).
//
// Hintergrund: upstream #661 (RAK4631, Brown-out korrumpiert die Einstellungen,
// Firmware stuerzt ab, einzige Rettung ist Flash formatieren) und #57
// (RF_POWER: 1 -> kein LAN, kein Serial, kein BLE). Die Ladepfade pruefen Marker
// und Groesse der Struktur (N-12), nicht den Inhalt.
//
//   pio test -e native -f test_settings_sanitize

#include <unity.h>

#include <string.h>
#include <math.h>
#include <stdio.h>

#include <settings_sanitize.h>

static const RadioLimits ESP32_LIMITS = { -9, 22, 400.0f, 960.0f, 0, 0, 17 };
static const RadioLimits NRF52_LIMITS = { 2, 22, 400.0e6f, 960.0e6f, 1, 1, 17 };

static int  g_log_calls;
static char g_last_field[32];

static void capture_log(const char *field, const char *, const char *)
{
    g_log_calls++;
    snprintf(g_last_field, sizeof(g_last_field), "%s", field);
}

void setUp(void)   { g_log_calls = 0; g_last_field[0] = 0; }
void tearDown(void) {}

static void test_gueltige_werte_bleiben_unveraendert(void)
{
    RadioParams p = { 20, 433.175f, 250.0f, 11, 6, 8 };
    TEST_ASSERT_EQUAL_INT(0, sanitize_radio_params(p, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(20, p.power);
    TEST_ASSERT_EQUAL_FLOAT(433.175f, p.freq);
    TEST_ASSERT_EQUAL_FLOAT(250.0f, p.bw);
    TEST_ASSERT_EQUAL_INT(11, p.sf);
    TEST_ASSERT_EQUAL_INT(6, p.cr);
    TEST_ASSERT_EQUAL_INT(8, p.country);
    TEST_ASSERT_EQUAL_INT(0, g_log_calls);
}

static void test_sentinels_bleiben_erhalten(void)
{
    // "nicht gesetzt": power -20, freq/bw/sf/cr 0 -- die Firmware loest sie selbst auf
    RadioParams p = { -20, 0.0f, 0.0f, 0, 0, 0 };
    TEST_ASSERT_EQUAL_INT(0, sanitize_radio_params(p, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(-20, p.power);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.freq);
    TEST_ASSERT_EQUAL_INT(0, p.sf);
}

static void test_power_ausserhalb_wird_zum_sentinel(void)
{
    RadioParams p = { 99, 433.175f, 250.0f, 11, 6, 0 };
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(p, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(-20, p.power);
    TEST_ASSERT_EQUAL_STRING("node_power", g_last_field);

    // #57: RF_POWER 1 liegt auf dem RAK (Min 2) ausserhalb, auf dem ESP32 (Min -9) nicht
    RadioParams r = { 1, 433175000.0f, 1.0f, 11, 2, 8 };
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(r, NRF52_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(-20, r.power);
    RadioParams e = { 1, 433.175f, 250.0f, 11, 6, 8 };
    TEST_ASSERT_EQUAL_INT(0, sanitize_radio_params(e, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(1, e.power);
}

static void test_frequenz_muell_und_nan(void)
{
    RadioParams p = { 20, 1.0e9f, 250.0f, 11, 6, 0 };
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(p, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.freq);

    RadioParams q = { 20, NAN, 250.0f, 11, 6, 0 };
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(q, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, q.freq);

    // nRF52 rechnet in Hz
    RadioParams r = { 20, 433175000.0f, 1.0f, 11, 2, 8 };
    TEST_ASSERT_EQUAL_INT(0, sanitize_radio_params(r, NRF52_LIMITS, capture_log));
}

static void test_bandbreite_je_plattform(void)
{
    RadioParams e = { 20, 433.175f, 300.0f, 11, 6, 0 };     // ESP32: nur 125/250/500
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(e, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.bw);

    RadioParams n = { 20, 433175000.0f, 250.0f, 11, 2, 0 };   // nRF52: Index 0..2, kHz-Wert ist Muell
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(n, NRF52_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, n.bw);

    RadioParams ok = { 20, 433175000.0f, 2.0f, 11, 2, 0 };
    TEST_ASSERT_EQUAL_INT(0, sanitize_radio_params(ok, NRF52_LIMITS, capture_log));
}

static void test_sf_und_cr_bereiche(void)
{
    RadioParams p = { 20, 433.175f, 250.0f, 13, 9, 0 };
    TEST_ASSERT_EQUAL_INT(2, sanitize_radio_params(p, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(0, p.sf);
    TEST_ASSERT_EQUAL_INT(0, p.cr);

    RadioParams n = { 20, 433175000.0f, 1.0f, 5, 6, 0 };    // nRF52: cr ist Index 1..4, 6 ist Muell; sf 5 auch
    TEST_ASSERT_EQUAL_INT(2, sanitize_radio_params(n, NRF52_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(0, n.cr);
    TEST_ASSERT_EQUAL_INT(0, n.sf);
}

static void test_country_index(void)
{
    RadioParams p = { 20, 433.175f, 250.0f, 11, 6, 17 };    // max_country ist exklusiv
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(p, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(0, p.country);
    RadioParams q = { 20, 433.175f, 250.0f, 11, 6, -1 };
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(q, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(0, q.country);
}

static void test_alles_muell_zaehlt_jedes_feld(void)
{
    RadioParams p = { 127, -5.0f, 7.0f, 99, 99, 200 };
    TEST_ASSERT_EQUAL_INT(6, sanitize_radio_params(p, ESP32_LIMITS, capture_log));
    TEST_ASSERT_EQUAL_INT(6, g_log_calls);
}

static void test_ohne_logger(void)
{
    RadioParams p = { 127, 433.175f, 250.0f, 11, 6, 0 };
    TEST_ASSERT_EQUAL_INT(1, sanitize_radio_params(p, ESP32_LIMITS, NULL));
}

static void test_max_hop_text_plausibilitaet(void)
{
    // CS-01: eine alte Settings-Datei / ein fehlender NVS-Key liefert 0 --
    // ohne diesen Pfad wuerde die Node mit Hop-Limit 0 senden.
    int v = 0;
    TEST_ASSERT_TRUE(sanitize_max_hop_text(v, capture_log));
    TEST_ASSERT_EQUAL_INT(4, v);
    TEST_ASSERT_EQUAL_INT(1, g_log_calls);
    TEST_ASSERT_EQUAL_STRING("max_hop_text", g_last_field);

    // 7 ist die on-air Obergrenze MAX_HOP_LIMIT, als Einstellung nicht erlaubt
    v = 7;
    TEST_ASSERT_TRUE(sanitize_max_hop_text(v, capture_log));
    TEST_ASSERT_EQUAL_INT(4, v);

    v = -3;
    TEST_ASSERT_TRUE(sanitize_max_hop_text(v, NULL));
    TEST_ASSERT_EQUAL_INT(4, v);

    // gueltige Werte bleiben unangetastet und melden nichts
    g_log_calls = 0;
    for (int good = 1; good <= 6; good++)
    {
        v = good;
        TEST_ASSERT_FALSE(sanitize_max_hop_text(v, capture_log));
        TEST_ASSERT_EQUAL_INT(good, v);
    }
    TEST_ASSERT_EQUAL_INT(0, g_log_calls);
}

static void test_1132_resolve_tx_power_sentinels(void)
{
    // #1132: both "not set" sentinels (0 pre-v4.35p, -20 since upstream
    // 50c1ce59) resolve to the board default; anything else -- including a
    // valid negative SX1262 setting -- passes through untouched.
    TEST_ASSERT_EQUAL_INT(22, resolve_tx_power(-20, 22));
    TEST_ASSERT_EQUAL_INT(22, resolve_tx_power(0, 22));
    TEST_ASSERT_EQUAL_INT(-9, resolve_tx_power(-9, 22));
    TEST_ASSERT_EQUAL_INT(10, resolve_tx_power(10, 22));
    TEST_ASSERT_EQUAL_INT(22, resolve_tx_power(22, 22));
    TEST_ASSERT_EQUAL_INT(10, resolve_tx_power(-20, 10));
}

static void test_cstring_terminator(void)
{
    char ok[10] = "DK5EN-14";
    TEST_ASSERT_FALSE(sanitize_cstring(ok, sizeof(ok)));
    TEST_ASSERT_EQUAL_STRING("DK5EN-14", ok);

    char bad[10];
    memset(bad, 'A', sizeof(bad));
    TEST_ASSERT_TRUE(sanitize_cstring(bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_INT(9, (int)strlen(bad));

    TEST_ASSERT_FALSE(sanitize_cstring(NULL, 10));
    TEST_ASSERT_FALSE(sanitize_cstring(bad, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_gueltige_werte_bleiben_unveraendert);
    RUN_TEST(test_sentinels_bleiben_erhalten);
    RUN_TEST(test_power_ausserhalb_wird_zum_sentinel);
    RUN_TEST(test_frequenz_muell_und_nan);
    RUN_TEST(test_bandbreite_je_plattform);
    RUN_TEST(test_sf_und_cr_bereiche);
    RUN_TEST(test_country_index);
    RUN_TEST(test_alles_muell_zaehlt_jedes_feld);
    RUN_TEST(test_ohne_logger);
    RUN_TEST(test_max_hop_text_plausibilitaet);
    RUN_TEST(test_1132_resolve_tx_power_sentinels);
    RUN_TEST(test_cstring_terminator);
    return UNITY_END();
}
