// MeshCom 5, Welle 4 (docs/meshcom5-campaign.md): der MH-Rahmen an die
// Telefon-App aus src/mh_phone.* (Konzept docs/meshcom5-topologie/ 4.9).
// env native_mh_phone: 128 Zeilen/512 Kanten/64 Slots/112 Horizont (S3/nRF52-
// Groessen, wie native_nbr_views).
//
// test_build_src=no: die Quellen kommen per #include (Muster wie
// test/test_nbr_views) -- mh_phone.cpp braucht dafuer nur nbr_matrix.cpp
// (nbrDistKm(), NBR_SNR_UNKNOWN); nbr_views.cpp wird fuer mhJsonBuild() nicht
// gebraucht (das ist der einzige Teil, der unter NATIVE_BUILD uebersetzt
// wird -- mhPhoneLive()/mhPhoneList*() sind Hardware und stehen hinter
// #ifndef NATIVE_BUILD).
//
// Weil mh_phone.cpp per #include in dieser Uebersetzungseinheit landet, ist
// die file-lokale mhRoundDist() hier sichtbar und direkt pruefbar (gleiches
// Muster wie test_nbr_views mit den internen Helfern von nbr_views.cpp).

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <ArduinoJson.h>

#include "../../src/nbr_matrix.cpp"
#include "../../src/mh_phone.cpp"

void setUp(void) {}
void tearDown(void) {}

// --- Helfer ------------------------------------------------------------------

// now_epoch fuer ein UTC-Kalenderdatum (proleptic Gregorian, keine Schaltsekunden --
// derselbe Vertrag wie gmtime_r/timegm, den mhJsonBuild() selbst benutzt).
static uint32_t epoch_for(int year, int mon, int day, int hour, int min, int sec)
{
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = year - 1900;
    tmv.tm_mon  = mon - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min  = min;
    tmv.tm_sec  = sec;
    return (uint32_t)timegm(&tmv);
}

static NbrMhView mkView(void)
{
    NbrMhView v;
    memset(&v, 0, sizeof(v));
    strcpy(v.call, "DK5EN-98");
    v.age_min = 5;
    v.sec     = 30;
    v.plt     = '!';
    v.hw      = 14;
    v.mod     = 0x21;
    v.rssi    = -97;
    v.snr     = -6;
    v.lat     = 48.2f;
    v.lon     = 16.3f;
    v.alt     = 200;
    v.pl      = 2;
    v.mesh    = 1;
    v.ncnt    = 5;
    v.hm_snr  = -13;
    v.role    = 'N';
    v.ex      = 1;
    v.nb      = 3;
    v.gw      = 0;
    v.via     = 0;
    v.fw      = 't';
    return v;
}

// Findet die Position eines Schluessels ("KEY":) im Rahmentext, oder -1.
static int key_pos(const char *json, const char *key)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    return p ? (int)(p - json) : -1;
}

// --- Test 1: 13 alte Felder -- Name, Reihenfolge und Werte wie die alte
// updateMheard() (mheard_functions.cpp, jetzt entfernt) sie fuer dieselben
// Werte geschrieben haette. ---------------------------------------------------

void test_old13_byte_identical_layout(void)
{
    NbrMhView v = mkView();
    uint32_t now_epoch = epoch_for(2026, 9, 26, 12, 0, 0);   // Kante (x,0) vor 5 min, Sekunde 30

    uint8_t buf[400] = {0};
    uint16_t n = mhJsonBuild(v, now_epoch, 48.1, 16.2, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_UINT8(0x44, buf[0]);

    const char *json = (const char *)(buf + 1);

    // DATE/TIME: now_epoch - 5*60 = 11:55:00, Sekunde aus v.sec (30) ersetzt.
    TEST_ASSERT_NOT_NULL(strstr(json, "\"TYP\":\"MH\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"CALL\":\"DK5EN-98\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"DATE\":\"2026-09-26\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"TIME\":\"11:55:30\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"PLT\":33"));    // '!' == 0x21 == 33
    TEST_ASSERT_NOT_NULL(strstr(json, "\"HW\":14"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"MOD\":33"));    // 0x21 == 33
    TEST_ASSERT_NOT_NULL(strstr(json, "\"RSSI\":-97"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"SNR\":-6"));
    // DIST: own (48.1,16.2) -> Nachbar (48.2,16.3), gerundet auf 0,1 km.
    float expect_dist = mhRoundDist((double)nbrDistKm(48.1f, 16.2f, 48.2f, 16.3f));
    char distpat[32];
    snprintf(distpat, sizeof(distpat), "\"DIST\":%.1f", (double)expect_dist);
    TEST_ASSERT_NOT_NULL(strstr(json, distpat));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"PL\":2"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"MESH\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"NCNT\":5"));

    // Reihenfolge: jeder der 13 alten Schluessel steht vor dem naechsten.
    const char *old_keys[13] = {"TYP", "CALL", "DATE", "TIME", "PLT", "HW", "MOD",
                                 "RSSI", "SNR", "DIST", "PL", "MESH", "NCNT"};
    int last = -1;
    for (int i = 0; i < 13; i++)
    {
        int p = key_pos(json, old_keys[i]);
        TEST_ASSERT_TRUE_MESSAGE(p > last, old_keys[i]);
        last = p;
    }
}

// --- Test 2: die 7 neuen Felder stehen alle HINTER den 13 alten (das ist die
// Voraussetzung dafuer, dass bleJsonFrameFailSoft() -- eigener Test
// test_ble_json_frame -- bei Ueberlaenge nur sie streicht). HM fehlt, wenn
// unbekannt; ROLE fehlt bei NA (role==0). ------------------------------------

void test_new7_after_old13_and_optional_fields(void)
{
    NbrMhView v = mkView();
    uint32_t now_epoch = epoch_for(2026, 9, 26, 12, 0, 0);

    uint8_t buf[400] = {0};
    uint16_t n = mhJsonBuild(v, now_epoch, 48.1, 16.2, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    const char *json = (const char *)(buf + 1);

    int last_old = key_pos(json, "NCNT");
    const char *new_keys[7] = {"AGE", "HM", "ROLE", "EX", "NB", "GW", "VIA"};
    for (int i = 0; i < 7; i++)
    {
        int p = key_pos(json, new_keys[i]);
        TEST_ASSERT_TRUE_MESSAGE(p > last_old, new_keys[i]);
    }

    TEST_ASSERT_NOT_NULL(strstr(json, "\"AGE\":5"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"HM\":-13"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"ROLE\":\"N\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"EX\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"NB\":3"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"GW\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"VIA\":0"));

    // HM fehlt, wenn NBR_SNR_UNKNOWN.
    NbrMhView v2 = v;
    v2.hm_snr = NBR_SNR_UNKNOWN;
    uint8_t buf2[400] = {0};
    uint16_t n2 = mhJsonBuild(v2, now_epoch, 48.1, 16.2, buf2, sizeof(buf2));
    TEST_ASSERT_GREATER_THAN(0, n2);
    TEST_ASSERT_NULL(strstr((const char *)(buf2 + 1), "\"HM\":"));

    // ROLE fehlt bei role == 0 (NA).
    NbrMhView v3 = v;
    v3.role = 0;
    uint8_t buf3[400] = {0};
    uint16_t n3 = mhJsonBuild(v3, now_epoch, 48.1, 16.2, buf3, sizeof(buf3));
    TEST_ASSERT_GREATER_THAN(0, n3);
    TEST_ASSERT_NULL(strstr((const char *)(buf3 + 1), "\"ROLE\":"));
}

// --- Test 3: FailSoft streicht bei Ueberlaenge zuerst die neuen Felder, nie
// die alten 13 -- End-to-End mit derselben Schluesselreihenfolge, wie
// mhJsonBuild() sie baut, aber mit einem kuenstlich aufgeblaehten neuen Feld
// (HM als String), weil kein echtes Feld von NbrMhView allein ueber
// BLE_JSON_PAYLOAD_MAX hinaustreibt (Konzept 4.9: 237 B im schlimmsten Fall,
// siehe test_frame_length_worst_case unten). Das ist derselbe Mechanismus
// (bleJsonFrameFailSoft(), eigener Test test_ble_json_frame), hier gegen
// GENAU die Reihenfolge von mhJsonBuild() gepruegt. -------------------------

void test_failsoft_drops_new_fields_first(void)
{
    JsonDocument doc;
    doc["TYP"]  = "MH";
    doc["CALL"] = "DK5EN-98";
    doc["DATE"] = "2026-09-26";
    doc["TIME"] = "11:55:30";
    doc["PLT"]  = 33;
    doc["HW"]   = 14;
    doc["MOD"]  = 33;
    doc["RSSI"] = -97;
    doc["SNR"]  = -6;
    doc["DIST"] = 1.2;
    doc["PL"]   = 2;
    doc["MESH"] = 1;
    doc["NCNT"] = 5;
    doc["AGE"]  = 5;
    // Kuenstlich ueberlang, um bleJsonFrameFailSoft() sicher ueber
    // BLE_JSON_PAYLOAD_MAX zu treiben -- kein reales NbrMhView-Feld kann das.
    char pad[260];
    memset(pad, 'x', sizeof(pad) - 1);
    pad[sizeof(pad) - 1] = 0;
    doc["HM"]   = pad;
    doc["ROLE"] = "N";
    doc["EX"]   = 1;
    doc["NB"]   = 3;
    doc["GW"]   = 0;
    doc["VIA"]  = 0;

    uint8_t buf[600] = {0};
    buf[0] = 0x44;
    uint16_t n = bleJsonFrameFailSoft(doc, buf, sizeof(buf), BLE_JSON_PAYLOAD_MAX);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_TRUE(n <= BLE_JSON_PAYLOAD_MAX + 1);   // +1: Kennbyte zaehlt hier mit

    const char *json = (const char *)(buf + 1);
    // alle 13 alten Felder ueberleben unveraendert.
    TEST_ASSERT_NOT_NULL(strstr(json, "\"TYP\":\"MH\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"CALL\":\"DK5EN-98\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"NCNT\":5"));
    // das aufgeblaehte neue Feld ist weg.
    TEST_ASSERT_NULL(strstr(json, "\"HM\":"));
    // ArduinoJson liefert gueltiges JSON (deserialisierbar).
    JsonDocument check;
    DeserializationError err = deserializeJson(check, json);
    TEST_ASSERT_EQUAL(DeserializationError::Ok, err.code());
}

// --- Test 4: kein Rahmen ohne gueltige Uhr (Jahr < 2025). -------------------

void test_no_frame_without_clock(void)
{
    NbrMhView v = mkView();
    uint32_t now_epoch = epoch_for(2020, 1, 1, 0, 0, 0);   // klar vor 2025
    uint8_t buf[400] = {0};
    uint16_t n = mhJsonBuild(v, now_epoch, 48.1, 16.2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT16(0, n);
}

// Advisor R1 (Welle 4 Gate): mhNodeEpoch() (nur unter #ifndef NATIVE_BUILD,
// hier am Host nicht erreichbar -- siehe Bericht) liefert bei ungestellter
// Uhr 0 statt eines mktime()-Ueberlaufwerts. Diese Zeile prueft die Haelfte,
// die der Host sehen kann: mhJsonBuild(v, 0, ...) -- der Wert, den
// mhNodeEpoch() jetzt zurueckgibt -- sendet ebenfalls nichts (1970 < 2025).
void test_no_frame_when_epoch_zero(void)
{
    NbrMhView v = mkView();
    uint8_t buf[400] = {0};
    uint16_t n = mhJsonBuild(v, 0, 48.1, 16.2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT16(0, n);
}

// Puffer zu klein (len < 2) liefert ebenfalls 0, ohne buf anzufassen.
void test_no_frame_on_tiny_buffer(void)
{
    NbrMhView v = mkView();
    uint32_t now_epoch = epoch_for(2026, 9, 26, 12, 0, 0);
    uint8_t buf[1] = {0xAA};
    uint16_t n = mhJsonBuild(v, now_epoch, 48.1, 16.2, buf, 1);
    TEST_ASSERT_EQUAL_UINT16(0, n);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);   // unangetastet
}

// --- Test 5: DIST-Rundungsfaelle (mhRoundDist(), dieselbe Herleitung wie die
// alte mheardRoundDist()). ---------------------------------------------------

void test_dist_rounding_cases(void)
{
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, mhRoundDist(-1.0));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, mhRoundDist(-0.001));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, mhRoundDist(0.0));
    TEST_ASSERT_EQUAL_FLOAT(1.2f, mhRoundDist(1.25));    // printf rundet zur geraden Ziffer
    TEST_ASSERT_EQUAL_FLOAT(0.1f, mhRoundDist(0.05));    // 0.05 liegt als double knapp darueber
    TEST_ASSERT_EQUAL_FLOAT(1.9f, mhRoundDist(1.95));   // 1.95 liegt als double knapp darunter
    TEST_ASSERT_EQUAL_FLOAT(12.3f, mhRoundDist(12.34));
    TEST_ASSERT_EQUAL_FLOAT(12.3f, mhRoundDist(12.349));
}

void test_dist_unknown_when_position_missing(void)
{
    NbrMhView v = mkView();
    uint32_t now_epoch = epoch_for(2026, 9, 26, 12, 0, 0);

    // eigene Position 0/0 = unbekannt.
    uint8_t buf[400] = {0};
    uint16_t n = mhJsonBuild(v, now_epoch, 0.0, 0.0, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr((const char *)(buf + 1), "\"DIST\":-1"));

    // fremde Position unbekannt (NAN).
    NbrMhView v2 = v;
    v2.lat = NAN;
    v2.lon = NAN;
    uint8_t buf2[400] = {0};
    uint16_t n2 = mhJsonBuild(v2, now_epoch, 48.1, 16.2, buf2, sizeof(buf2));
    TEST_ASSERT_GREATER_THAN(0, n2);
    TEST_ASSERT_NOT_NULL(strstr((const char *)(buf2 + 1), "\"DIST\":-1"));
}

// --- Test 6: Rahmenlaenge im schlimmsten Fall -- Konzept 4.9 nennt 225 B
// typisch, 232 B mit allen Kodierungen am Maximum, 237 B mit AGE 65535 und
// DIST in Exponentform (gemessen mit ArduinoJson 7.4.3). Die Grenze ist
// BLE_JSON_PAYLOAD_MAX (244) plus 1 Byte Kennbyte; die genauen Bytezahlen
// haengen an der ArduinoJson-Version und werden hier gemessen, nicht
// vorausgesetzt. ------------------------------------------------------------

void test_frame_length_worst_case(void)
{
    NbrMhView v = mkView();
    uint32_t now_epoch = epoch_for(2026, 9, 26, 12, 0, 0);

    // typisch (mkView()-Werte, siehe oben).
    uint8_t buf_typ[400] = {0};
    uint16_t n_typ = mhJsonBuild(v, now_epoch, 48.1, 16.2, buf_typ, sizeof(buf_typ));
    TEST_ASSERT_GREATER_THAN(0, n_typ);
    TEST_ASSERT_LESS_OR_EQUAL(BLE_JSON_PAYLOAD_MAX + 1, n_typ);
    printf("[mh_phone] typical frame length: %u B (concept: 225 B)\n", (unsigned)n_typ);

    // Maximum: jedes Feld an seiner Typgrenze (nicht am Praxiswert), 9-Zeichen-
    // Rufzeichen, AGE 65535, Sekunde bekannt, Positionen nahe Antipoden fuer
    // die groesstmoegliche DIST.
    NbrMhView vm;
    memset(&vm, 0, sizeof(vm));
    strcpy(vm.call, "DL2ABCDEF");   // 9 Zeichen, NBR_CALL_LEN-1
    vm.age_min = 65535;
    vm.sec     = 59;
    vm.plt     = '@';
    vm.hw      = 255;
    vm.mod     = 255;
    vm.rssi    = -32768;
    vm.snr     = -128;
    vm.lat     = 0.001f;
    vm.lon     = 0.001f;
    vm.alt     = -30000;
    vm.pl      = 255;
    vm.mesh    = 1;
    vm.ncnt    = 255;
    vm.hm_snr  = -127;
    vm.role    = 'S';
    vm.ex      = 255;
    vm.nb      = 255;
    vm.gw      = 1;
    vm.via     = 1;
    vm.fw      = 'z';

    uint8_t buf_max[400] = {0};
    // fast antipodale eigene Position -> groesstmoegliche Haversine-Distanz.
    uint16_t n_max = mhJsonBuild(vm, now_epoch, -0.001, 179.999, buf_max, sizeof(buf_max));
    TEST_ASSERT_GREATER_THAN(0, n_max);
    TEST_ASSERT_LESS_OR_EQUAL(BLE_JSON_PAYLOAD_MAX + 1, n_max);
    printf("[mh_phone] max-fields frame length: %u B (concept: 232-237 B)\n", (unsigned)n_max);
    printf("[mh_phone] max-fields DIST substring check: %s\n", strstr((const char *)(buf_max + 1), "\"DIST\":") ? "present" : "MISSING");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_old13_byte_identical_layout);
    RUN_TEST(test_new7_after_old13_and_optional_fields);
    RUN_TEST(test_failsoft_drops_new_fields_first);
    RUN_TEST(test_no_frame_without_clock);
    RUN_TEST(test_no_frame_when_epoch_zero);
    RUN_TEST(test_no_frame_on_tiny_buffer);
    RUN_TEST(test_dist_rounding_cases);
    RUN_TEST(test_dist_unknown_when_position_missing);
    RUN_TEST(test_frame_length_worst_case);
    return UNITY_END();
}
