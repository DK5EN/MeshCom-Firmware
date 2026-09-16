// R2-01. Die Umformung einer MHeard-Zeile hatte bis hierher keinen
// ausfuehrbaren Test: sie lief durch snprintf und eine zeichenweise
// String-Zerlegung in command-/mheard_functions.cpp, die keine native
// Umgebung uebersetzt. Genau deshalb sind hier die Faelle drin, in denen sich
// Text- und Datensatzfassung unterscheiden KOENNTEN.

#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "../../src/mheard_record.h"

static MheardRecord rec;
static char buf[32];

void setUp(void) { memset(&rec, 0, sizeof(rec)); memset(buf, 0, sizeof(buf)); }
void tearDown(void) {}

// --- Groesse: der ganze Zweck der Zeile ----------------------------------

void test_record_is_20_bytes_not_60(void)
{
    // 60 Byte je Eintrag waren es als Text. Faellt das hier um, ist die
    // Ersparnis weg und die Zeile hat ihren Grund verloren.
    TEST_ASSERT_EQUAL_UINT32(20, (uint32_t)sizeof(MheardRecord));
}

// --- Datum/Uhrzeit: Hin- und Rueckweg muessen exakt sein -----------------

void test_date_round_trip_is_exact(void)
{
    TEST_ASSERT_TRUE(mheardSetDate(rec, "2026-09-16"));
    TEST_ASSERT_EQUAL_UINT8(26, rec.mr_year);
    TEST_ASSERT_EQUAL_UINT8(9, rec.mr_month);
    TEST_ASSERT_EQUAL_UINT8(16, rec.mr_day);
    mheardFormatDate(rec, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("2026-09-16", buf);
}

void test_time_round_trip_is_exact(void)
{
    TEST_ASSERT_TRUE(mheardSetTime(rec, "07:05:09"));
    TEST_ASSERT_EQUAL_UINT8(7, rec.mr_hour);
    TEST_ASSERT_EQUAL_UINT8(5, rec.mr_minute);
    TEST_ASSERT_EQUAL_UINT8(9, rec.mr_second);
    mheardFormatTime(rec, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("07:05:09", buf);   // fuehrende Nullen bleiben
}

void test_every_day_of_a_leap_year_round_trips(void)
{
    static const int mdays[12] = {31,29,31,30,31,30,31,31,30,31,30,31};
    char in[16];
    for (int m = 1; m <= 12; m++)
        for (int d = 1; d <= mdays[m-1]; d++)
        {
            snprintf(in, sizeof(in), "2024-%02d-%02d", m, d);
            TEST_ASSERT_TRUE(mheardSetDate(rec, in));
            mheardFormatDate(rec, buf, sizeof(buf));
            TEST_ASSERT_EQUAL_STRING(in, buf);
        }
}

void test_every_second_of_a_day_round_trips(void)
{
    char in[16];
    for (int h = 0; h < 24; h++)
        for (int mi = 0; mi < 60; mi += 7)
            for (int s = 0; s < 60; s += 11)
            {
                snprintf(in, sizeof(in), "%02d:%02d:%02d", h, mi, s);
                TEST_ASSERT_TRUE(mheardSetTime(rec, in));
                mheardFormatTime(rec, buf, sizeof(buf));
                TEST_ASSERT_EQUAL_STRING(in, buf);
            }
}

// --- Falsches Format wird GANZ abgelehnt, nicht halb uebernommen ---------

void test_malformed_date_is_refused_whole(void)
{
    rec.mr_year = 99; rec.mr_month = 9; rec.mr_day = 9;
    TEST_ASSERT_FALSE(mheardSetDate(rec, "2026/09/16"));   // falsche Trenner
    TEST_ASSERT_FALSE(mheardSetDate(rec, "2026-09-1"));    // zu kurz
    TEST_ASSERT_FALSE(mheardSetDate(rec, ""));
    TEST_ASSERT_FALSE(mheardSetDate(rec, 0));
    TEST_ASSERT_FALSE(mheardSetDate(rec, "19xx-09-16"));   // keine Ziffern
    TEST_ASSERT_FALSE(mheardSetDate(rec, "1999-09-16"));   // vor 2000
    // nichts davon darf den Datensatz angefasst haben
    TEST_ASSERT_EQUAL_UINT8(99, rec.mr_year);
    TEST_ASSERT_EQUAL_UINT8(9, rec.mr_month);
    TEST_ASSERT_EQUAL_UINT8(9, rec.mr_day);
}

void test_malformed_time_is_refused_whole(void)
{
    rec.mr_hour = 77; rec.mr_minute = 77; rec.mr_second = 77;
    TEST_ASSERT_FALSE(mheardSetTime(rec, "07-05-09"));
    TEST_ASSERT_FALSE(mheardSetTime(rec, "7:5:9"));
    TEST_ASSERT_FALSE(mheardSetTime(rec, "07:05:0"));
    TEST_ASSERT_FALSE(mheardSetTime(rec, 0));
    TEST_ASSERT_EQUAL_UINT8(77, rec.mr_hour);
    TEST_ASSERT_EQUAL_UINT8(77, rec.mr_minute);
    TEST_ASSERT_EQUAL_UINT8(77, rec.mr_second);
}

// --- Entfernung: exakt die Genauigkeit, die der Textsatz hatte -----------

void test_dist_is_rounded_exactly_like_the_old_text_format(void)
{
    // Der alte Satz schrieb %.1lf und las genau das zurueck. Das JSON-Register
    // gibt den Wert ROH aus, also entscheidet diese Rundung darueber, ob sich
    // die Ausgabe aendert.
    char a[32], b[32];
    static const double v[] = {0.0, 0.04, 0.05, 1.24, 1.25, 1.26, 999.94, 999.95,
                               12345.67, 4.449, 4.45, 88.888};
    for (size_t i = 0; i < sizeof(v)/sizeof(v[0]); i++)
    {
        snprintf(a, sizeof(a), "%.1lf", v[i]);                       // alt
        snprintf(b, sizeof(b), "%.1f", mheardRoundDist(v[i]));       // neu
        TEST_ASSERT_EQUAL_STRING(a, b);
    }
}

void test_negative_dist_keeps_its_not_yet_computed_meaning(void)
{
    // mh_dist < 0 heisst "noch nicht berechnet" (lora_functions.cpp:771) und
    // wird spaeter durch den gespeicherten Wert ersetzt. Das darf die Rundung
    // nicht in 0 verwandeln, sonst sieht "unbekannt" wie "0 m" aus.
    TEST_ASSERT_TRUE(mheardRoundDist(-1.0) < 0.0f);
}

// --- Zielpuffer zu klein: leer statt halb -------------------------------

void test_short_output_buffer_yields_empty_not_partial(void)
{
    char small[4] = {'x','x','x','x'};
    mheardSetDate(rec, "2026-09-16");
    mheardFormatDate(rec, small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("", small);
    char small2[4] = {'x','x','x','x'};
    mheardFormatTime(rec, small2, sizeof(small2));
    TEST_ASSERT_EQUAL_STRING("", small2);
}

// --- uebernommen aus test/test_decodemheard (PT-01) ----------------------
//
// Jene Suite pruefte den Textparser, den R2-01 entfernt hat. Vier ihrer sieben
// Faelle pruefen Zerlegungsfehler, die es ohne Text NICHT MEHR GEBEN KANN
// (fehlende Pipe nach dem Typbyte, fehlende Pipe nach dem Datum, fehlende
// Endfelder). Die uebrigen Anliegen ueberleben das Format und stehen deshalb
// hier weiter -- sie mit dem Parser zu loeschen waere Deckung verlieren, nicht
// Ballast abwerfen.

void test_negative_rssi_and_snr_survive_the_record(void)
{
    // Aus test_negativer_rssi_und_snr. Der Textsatz trug Vorzeichen durch
    // toInt(); der Datensatz muss dafuer die richtige Breite UND das richtige
    // Vorzeichen haben. Mit uint8_t mr_snr waere -20 zu 236 geworden.
    rec.mr_rssi = -128;
    rec.mr_snr  = -20;
    TEST_ASSERT_EQUAL_INT16(-128, rec.mr_rssi);
    TEST_ASSERT_EQUAL_INT8(-20, rec.mr_snr);
    TEST_ASSERT_TRUE(rec.mr_rssi < 0);
    TEST_ASSERT_TRUE(rec.mr_snr < 0);
}

void test_payload_type_survives_as_a_char(void)
{
    // Aus test_payload_type_text. Der Typ ist EIN Zeichen, kein Zahlenfeld.
    rec.mr_type = ':';
    TEST_ASSERT_EQUAL_CHAR(':', rec.mr_type);
    rec.mr_type = '@';
    TEST_ASSERT_EQUAL_CHAR('@', rec.mr_type);
}

void test_garbage_record_does_not_crash_or_overrun(void)
{
    // Aus test_garbage_bytes_stuerzt_nicht_ab. Ein Datensatz kann weiterhin
    // Unsinn enthalten -- /mheard.dat wird roh eingelesen. Die Formatierer
    // duerfen daran nicht ueberlaufen; sie schreiben feste Breiten.
    memset(&rec, 0xFF, sizeof(rec));
    char guard[16];
    memset(guard, 0x7E, sizeof(guard));
    mheardFormatDate(rec, guard, 11);
    TEST_ASSERT_EQUAL_size_t(10u, strlen(guard));    // feste Breite, wie vorher
    TEST_ASSERT_EQUAL_UINT8(0x7E, (uint8_t)guard[11]); // nichts dahinter angefasst

    memset(guard, 0x7E, sizeof(guard));
    mheardFormatTime(rec, guard, 9);
    TEST_ASSERT_EQUAL_size_t(8u, strlen(guard));
    TEST_ASSERT_EQUAL_UINT8(0x7E, (uint8_t)guard[9]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_record_is_20_bytes_not_60);
    RUN_TEST(test_date_round_trip_is_exact);
    RUN_TEST(test_time_round_trip_is_exact);
    RUN_TEST(test_every_day_of_a_leap_year_round_trips);
    RUN_TEST(test_every_second_of_a_day_round_trips);
    RUN_TEST(test_malformed_date_is_refused_whole);
    RUN_TEST(test_malformed_time_is_refused_whole);
    RUN_TEST(test_dist_is_rounded_exactly_like_the_old_text_format);
    RUN_TEST(test_negative_dist_keeps_its_not_yet_computed_meaning);
    RUN_TEST(test_short_output_buffer_yields_empty_not_partial);
    RUN_TEST(test_negative_rssi_and_snr_survive_the_record);
    RUN_TEST(test_payload_type_survives_as_a_char);
    RUN_TEST(test_garbage_record_does_not_crash_or_overrun);
    return UNITY_END();
}
