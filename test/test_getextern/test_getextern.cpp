// Native Testsuite fuer getExtern()/handleExternTelemetry() -- die
// unauthenticated LAN-JSON-Eingabe des EXTUDP-Protokolls
// (docs/ext_udp_telemetry.md, docs/ext_udp_telemetry_guide.md).
//
// PT-01 (BACKLOG.md #3.8j): dieser Eingang ist ohne Authentifizierung vom
// LAN aus erreichbar und hatte bislang keinen Test. getExtern() decodiert
// {"type":"msg"|"tele", ...} und ruft fuer "tele" das statische
// handleExternTelemetry() auf, das die Werte direkt in
// meshcom_settings.node_temp/... schreibt (die naechste Positionsbeacon
// uebernimmt sie 1:1, ununterscheidbar von echten Sensordaten) und sofort
// einen Beacon anstoesst (sendPosition()). Fuer "msg" baut es einen
// ":{dst}payload"-Rahmen und ruft sendMessage().
//
// sendMessage()/sendPosition() sind hier Recording-Stubs (siehe unten) --
// FL-01 (Beacon-Ratenbegrenzung) sitzt INNERHALB von sendPosition() und wird
// von diesem Stub nicht nachgebildet; hier wird nur gezaehlt, wie oft
// handleExternTelemetry() sendPosition() pro Telemetrie-Datagramm aufruft.
//
//   pio test -e native_extern -f test_getextern

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include <Arduino.h>
#include <extudp_functions.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/test_getextern/stubs (Obermenge von test/support)

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp -----------
// (identisches Muster zu test/test_hey_report/test_hey_report.cpp: dieselbe
// aprs_functions.cpp wird hier mit reinkompiliert, siehe platformio.ini
// [env:native_extern] build_src_filter.)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

// ---- Sensor-Erkennungs-Flags (loop_functions_extern.h) --------------------
// handleExternTelemetry() liest genau diese vier, um ein Node mit echter
// Sensorhardware NIE zu ueberschreiben.
bool bmx_found = false;
bool bmp3_found = false;
bool aht20_found = false;
bool sht21_found = false;

// ---- Recording-Stub fuer sendMessage() -------------------------------------
// getExtern() ruft dies fuer den "msg"-Pfad mit dem fertig gebauten
// ":{dst}payload"-Rahmen und dessen strlen().
static std::string g_sent_msg_text;
static int g_sent_msg_len = -1;
static int g_sendMessage_calls = 0;

void sendMessage(char *msg_text, int len)
{
    g_sent_msg_text.assign(msg_text, msg_text + len);
    g_sent_msg_len = len;
    g_sendMessage_calls++;
}

// ---- Recording-Stub fuer sendPosition() ------------------------------------
// handleExternTelemetry() ruft dies fuer den "tele"-Pfad, um sofort einen
// Beacon mit den neuen Werten rauszuschicken.
struct SendPositionArgs
{
    unsigned long intervall = 0;
    double lat = 0.0;
    char lat_c = 0;
    double lon = 0.0;
    char lon_c = 0;
    int alt = 0;
    float press = 0, hum = 0, temp = 0, temp2 = 0, gasres = 0, co2 = 0;
    int qfe = 0;
    float qnh = 0;
};
static int g_sendPosition_calls = 0;
static SendPositionArgs g_last_sendPosition;

void sendPosition(unsigned long intervall, double lat, char lat_c, double lon, char lon_c, int alt,
                   float press, float hum, float temp, float temp2, float gasres, float co2,
                   int qfe, float qnh)
{
    g_sendPosition_calls++;
    g_last_sendPosition = {intervall, lat, lat_c, lon, lon_c, alt, press, hum, temp, temp2, gasres, co2, qfe, qnh};
}

// ---------------------------------------------------------------- Fixtures

void setUp(void)
{
    g_sent_msg_text.clear();
    g_sent_msg_len = -1;
    g_sendMessage_calls = 0;
    g_sendPosition_calls = 0;
    g_last_sendPosition = SendPositionArgs{};
    bmx_found = bmp3_found = aht20_found = sht21_found = false;
    meshcom_settings = s_meshcom_settings{};
    Serial.clear();
}

void tearDown(void) {}

// getExtern() nimmt unsigned char[] entgegen -- niemals direkt auf ein
// String-Literal casten (das waere ein const-Wegwurf); stattdessen in einen
// eigenen, veraenderbaren Puffer kopieren, wie es echte UDP-Empfangspuffer
// auch waeren.
static void callGetExtern(const std::string &json, int len)
{
    std::vector<unsigned char> buf(json.begin(), json.end());
    buf.push_back(0);
    getExtern(buf.data(), len);
}

static void callGetExtern(const std::string &json)
{
    callGetExtern(json, (int)json.size());
}

// ---------------------------------------------------- "msg"-Pfad: Happy path

static void test_valid_msg_sendet_einmal_mit_erwartetem_dst_und_text(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE5BYE-1","msg":"Test 1 2 3"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING(":{OE5BYE-1}Test 1 2 3", g_sent_msg_text.c_str());
    TEST_ASSERT_EQUAL_INT((int)strlen(":{OE5BYE-1}Test 1 2 3"), g_sent_msg_len);
    TEST_ASSERT_EQUAL_INT(0, g_sendPosition_calls);
}

static void test_dst_stern_wildcard_wird_durchgereicht(void)
{
    callGetExtern(R"({"type": "msg", "dst": "*", "msg": "an alle"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING(":{*}an alle", g_sent_msg_text.c_str());
}

// -------------------------------------------------- "tele"-Pfad: Happy path

static void test_valid_tele_aktualisiert_settings_und_sendet_position_einmal(void)
{
    callGetExtern(R"({"type":"tele","temp":23.3,"hum":60,"press":1018.5,)"
                  R"("temp2":5.5,"qnh":1020.1,"gasres":123.4,"co2":456})");

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_INT(1, g_sendPosition_calls);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.3f, meshcom_settings.node_temp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, meshcom_settings.node_hum);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1018.5f, meshcom_settings.node_press);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.5f, meshcom_settings.node_temp2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1020.1f, meshcom_settings.node_press_asl);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 123.4f, meshcom_settings.node_gas_res);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 456.0f, meshcom_settings.node_co2);

    // sendPosition() liest genau die Felder zurueck, die handleExternTelemetry()
    // gerade geschrieben hat (Push-Beacon mit den neuen Werten).
    TEST_ASSERT_EQUAL_UINT32(0x9999u, (uint32_t)g_last_sendPosition.intervall);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, meshcom_settings.node_temp, g_last_sendPosition.temp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, meshcom_settings.node_press_asl, g_last_sendPosition.qnh);
}

static void test_tele_ohne_erkannte_felder_sendet_keine_position(void)
{
    callGetExtern(R"({"type":"tele","unbekannt":1})");

    TEST_ASSERT_EQUAL_INT(0, g_sendPosition_calls);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, meshcom_settings.node_temp);
}

static void test_tele_partiell_aktualisiert_nur_vorhandene_felder(void)
{
    callGetExtern(R"({"type":"tele","temp":21.0})");

    TEST_ASSERT_EQUAL_INT(1, g_sendPosition_calls);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.0f, meshcom_settings.node_temp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, meshcom_settings.node_hum);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, meshcom_settings.node_press);
}

static void test_sendposition_einmal_pro_tele_datagramm(void)
{
    // FL-01 begrenzt die Beacon-Rate INNERHALB von sendPosition() -- hier
    // gestubbt, also zaehlt jeder Aufruf. Zwei Datagramme -> zwei Aufrufe.
    callGetExtern(R"({"type":"tele","temp":1.0})");
    callGetExtern(R"({"type":"tele","temp":2.0})");

    TEST_ASSERT_EQUAL_INT(2, g_sendPosition_calls);
}

static void test_echte_sensorhardware_ueberspringt_extern_telemetrie(void)
{
    bmx_found = true;
    callGetExtern(R"({"type":"tele","temp":99.0})");

    TEST_ASSERT_EQUAL_INT(0, g_sendPosition_calls);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, meshcom_settings.node_temp);
}

// --------------------------------------------------------- Routing-Grenzen
// "type" wird NUR fuer den Tele-Zweig geprueft (strcmp == "tele"). Jeder
// andere Wert -- oder ein fehlendes "type" ueberhaupt -- faellt in den
// msg-Zweig durch, solange dst/msg vorhanden sind. Dokumentiertes
// IST-Verhalten, kein per TEST_IGNORE markierter Fund: "type" ist fuer
// diesen Zweig nicht mehr als ein Nicht-"tele"-Merker.

static void test_unbekannter_type_mit_dst_und_msg_wird_trotzdem_gesendet(void)
{
    callGetExtern(R"({"type":"foo","dst":"OE5BYE-1","msg":"trotzdem"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING(":{OE5BYE-1}trotzdem", g_sent_msg_text.c_str());
}

static void test_fehlendes_type_feld_mit_dst_und_msg_wird_trotzdem_gesendet(void)
{
    callGetExtern(R"({"dst":"OE5BYE-1","msg":"kein type feld"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
}

// ----------------------------------------------------------- Fehlende Felder

static void test_fehlendes_dst_wird_verworfen(void)
{
    callGetExtern(R"({"type":"msg","msg":"ohne dst"})");
    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
}

static void test_fehlendes_msg_wird_verworfen(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE5BYE-1"})");
    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
}

static void test_beide_felder_fehlen_wird_verworfen(void)
{
    callGetExtern(R"({"type":"foo"})");
    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_INT(0, g_sendPosition_calls);
}

// --------------------------------------------------------- Garbage-Eingaben

static void test_nicht_json_wird_ohne_absturz_verworfen(void)
{
    callGetExtern("das ist kein JSON {{{");

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_INT(0, g_sendPosition_calls);
}

static void test_leerer_puffer_wird_ohne_absturz_verworfen(void)
{
    callGetExtern("", 0);

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_INT(0, g_sendPosition_calls);
}

// --------------------------------------------------------------- Laengen

static void test_zu_langes_dst_wird_verworfen(void)
{
    // dst-Schranke ist 9 Zeichen (strlen(dst) > 9 -> verworfen).
    callGetExtern(R"({"type":"msg","dst":"1234567890","msg":"hi"})");
    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
}

static void test_zu_langes_msg_wird_ohne_ueberlauf_verworfen(void)
{
    // msg-Schranke ist 150 Zeichen. Deutlich darueber (1000) prueft, dass
    // ein Feld weit groesser als jeder interne Puffer sauber abgewiesen
    // wird, statt in val[161] (getExtern()) formatiert zu werden.
    std::string longMsg(1000, 'A');
    callGetExtern(std::string(R"({"type":"msg","dst":"A","msg":")") + longMsg + R"("})");

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
}

// ------------------------------------------------ len wird respektiert

static void test_len_kuerzer_als_puffer_bricht_den_parse_sauber_ab(void)
{
    // Ein vollstaendiges, gueltiges JSON-Dokument -- aber len zeigt mitten
    // hinein (vor die schliessende Klammer). deserializeJson(doc, buf, len)
    // darf NICHT ueber len hinauslesen: das muss als unvollstaendige
    // Eingabe scheitern, nicht als Treffer auf den NUL-terminierten Rest.
    std::string full = R"({"type":"msg","dst":"OE5BYE-1","msg":"Test 1 2 3"})";
    callGetExtern(full, (int)full.size() - 5);

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
}

static void test_len_ignoriert_bytes_hinter_len(void)
{
    // Puffer traegt gueltiges JSON, gefolgt von Muell, der NICHT zu len
    // gehoert. getExtern() muss exakt len respektieren (nicht strlen(incoming)).
    std::string full = R"({"type":"msg","dst":"OE5BYE-1","msg":"nur das hier"})";
    std::string withTrailingJunk = full + "GARBAGE-DAHINTER-DARF-NICHT-GELESEN-WERDEN";

    callGetExtern(withTrailingJunk, (int)full.size());

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING(":{OE5BYE-1}nur das hier", g_sent_msg_text.c_str());
}

// --------------------------------------------------- Verschachtelte Extras

static void test_verschachtelte_zusatzschluessel_werden_ignoriert(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE5BYE-1","msg":"kern",)"
                  R"("extra":{"foo":"bar","nested":[1,2,3]},"another":123})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING(":{OE5BYE-1}kern", g_sent_msg_text.c_str());
}

// --------------------------------------------- Numerische Felder als String

static void test_tele_numerisches_feld_als_json_string(void)
{
    // ArduinoJson konvertiert einen als String gespeicherten, numerisch
    // aussehenden Wert bei .as<float>() -- dokumentiert hier das tatsaechliche
    // Verhalten der Bibliothek (kein Fund in getExtern()/handleExternTelemetry()
    // selbst: diese Funktionen delegieren die Konvertierung vollstaendig an
    // ArduinoJson).
    callGetExtern(R"({"type":"tele","temp":"23.5"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendPosition_calls);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.5f, meshcom_settings.node_temp);
}

// ====================================================================
// PT-01-Funde: echte Fehlverhalten, NICHT behoben (Brief-Regel 4) --
// als fehlschlagende, ignorierte Faelle dokumentiert.
// ====================================================================

// FUND 1 -- Sentinel-Kollision: aprsmsg.msg_payload wird mit "none" als
// "nichts gesetzt"-Marker initialisiert (extudp_functions.cpp:~248). Ein
// legitimes JSON-Feld "msg":"none" landet nach der Zuweisung im selben
// String und wird von der nachfolgenden Pruefung
// `if(aprsmsg.msg_payload == "none")` als "kein Payload gesetzt"
// missverstanden -- die Nachricht wird verworfen, obwohl dst und msg beide
// gueltig, vorhanden und im Laengenlimit waren.
static void test_FUND_msg_gleich_sentinel_none_wird_still_verworfen(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE5BYE-1","msg":"none"})");

    TEST_IGNORE_MESSAGE(
        "PT-01 finding: getExtern() (extudp_functions.cpp ~L242, ~L275) "
        "initializes aprsmsg.msg_payload to the literal string \"none\" as its "
        "own \"nothing set yet\" sentinel, then -- after validating and "
        "assigning dst/msg from the JSON -- checks "
        "`if(aprsmsg.msg_payload == \"none\") return;`. A legitimate JSON "
        "message whose text is exactly \"none\" (dst and msg both present and "
        "within the length limits) collides with that sentinel: "
        "sendMessage() is never called, no error is logged beyond a "
        "misleading \"wrong JSON to send message\" line, and the sender gets "
        "no indication their message was dropped. Not fixed here (tests "
        "only, per brief).");
}

// FUND 2 -- Stille Kuerzung bei kombinierter Maximallaenge: dst darf bis zu
// 9 Zeichen, msg bis zu 150 Zeichen lang sein (je einzeln geprueft). Der
// Ausgaberahmen ":{%s}%s" wird aber mit `snprintf(val, 160, ...)` in einen
// char val[161] geschrieben. Bei maximaler dst- UND msg-Laenge braucht der
// Rahmen 3+9+150 = 162 Zeichen -- mehr als die 159 Nutzzeichen, die
// snprintf(...,160,...) schreiben darf. snprintf selbst ist bounds-sicher
// (kein Speicherueberlauf), ABER: der Rueckgabewert wird nicht geprueft,
// und die letzten Zeichen der Nachricht werden ersatzlos und ohne jede
// Fehlermeldung abgeschnitten, bevor sendMessage() sie verschickt.
static void test_FUND_maximale_dst_und_msg_laenge_kuerzt_still_im_val_puffer(void)
{
    std::string dst(9, '1');            // an der 9-Zeichen-Schranke
    std::string msg(150, 'M');          // an der 150-Zeichen-Schranke
    callGetExtern(std::string(R"({"type":"msg","dst":")") + dst + R"(","msg":")" + msg + R"("})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);   // sent -- just not all of msg, see below

    TEST_IGNORE_MESSAGE(
        "PT-01 finding: dst is allowed up to 9 chars and msg up to 150 chars "
        "(each checked separately in getExtern()), but the \":{dst}msg\" "
        "frame needs 3+len(dst)+len(msg) bytes -- 162 at both maxima -- and "
        "`snprintf(val, 160, ...)` (extudp_functions.cpp ~L297, val is "
        "char[161]) can write only 159 of them. snprintf's own size bound "
        "keeps this memory-safe (no overrun), but its return value is never "
        "checked: the last 3 characters of msg are silently dropped from the "
        "sent frame, with no error surfaced to the caller or the LAN sender. "
        "Not fixed here (tests only, per brief).");
}

// FUND 3 -- Eingebettetes NUL im msg-Feld: \u0000 innerhalb der
// JSON-Payload dekodiert korrekt zu einem rohen NUL-Byte, aber
// inputJson["msg"] liefert einen C-String (const char*), und jede
// nachfolgende Verarbeitung (strlen() fuer die Laengenpruefung, die
// Arduino-String-Zuweisung aprsmsg.msg_payload = msg, und am Ende
// snprintf("%s", ...)) terminiert an genau diesem Byte. Alles nach dem
// eingebetteten NUL wird kommentarlos verschluckt -- exakt der
// "eine Byte, das die Bedeutung jedes strlen/snprintf/String aendert"-Fall,
// den der PT-01-Testmassstab (BACKLOG.md #3.8j) benennt.
static void test_FUND_eingebettetes_nul_im_msg_kuerzt_still(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE5BYE-1","msg":"ab\u0000cd"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);   // sent -- just a truncated payload

    TEST_IGNORE_MESSAGE(
        "PT-01 finding: msg=\"ab\\u0000cd\" JSON-decodes to a 5-byte payload "
        "(\"ab\", NUL, \"cd\"), but getExtern() reads inputJson[\"msg\"] as a "
        "const char* and every step downstream -- the strlen() length check, "
        "the Arduino String assignment aprsmsg.msg_payload = msg, and the "
        "final snprintf(\"%s\",...) building val -- stops at the embedded "
        "NUL. \"cd\" is silently dropped: sendMessage() ships "
        "\":{OE5BYE-1}ab\" (13 bytes) instead of a frame carrying the "
        "sender\'s full 5-byte payload, and nothing signals the truncation. "
        "Not fixed here (tests only, per brief).");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_valid_msg_sendet_einmal_mit_erwartetem_dst_und_text);
    RUN_TEST(test_dst_stern_wildcard_wird_durchgereicht);

    RUN_TEST(test_valid_tele_aktualisiert_settings_und_sendet_position_einmal);
    RUN_TEST(test_tele_ohne_erkannte_felder_sendet_keine_position);
    RUN_TEST(test_tele_partiell_aktualisiert_nur_vorhandene_felder);
    RUN_TEST(test_sendposition_einmal_pro_tele_datagramm);
    RUN_TEST(test_echte_sensorhardware_ueberspringt_extern_telemetrie);

    RUN_TEST(test_unbekannter_type_mit_dst_und_msg_wird_trotzdem_gesendet);
    RUN_TEST(test_fehlendes_type_feld_mit_dst_und_msg_wird_trotzdem_gesendet);

    RUN_TEST(test_fehlendes_dst_wird_verworfen);
    RUN_TEST(test_fehlendes_msg_wird_verworfen);
    RUN_TEST(test_beide_felder_fehlen_wird_verworfen);

    RUN_TEST(test_nicht_json_wird_ohne_absturz_verworfen);
    RUN_TEST(test_leerer_puffer_wird_ohne_absturz_verworfen);

    RUN_TEST(test_zu_langes_dst_wird_verworfen);
    RUN_TEST(test_zu_langes_msg_wird_ohne_ueberlauf_verworfen);

    RUN_TEST(test_len_kuerzer_als_puffer_bricht_den_parse_sauber_ab);
    RUN_TEST(test_len_ignoriert_bytes_hinter_len);

    RUN_TEST(test_verschachtelte_zusatzschluessel_werden_ignoriert);

    RUN_TEST(test_tele_numerisches_feld_als_json_string);

    RUN_TEST(test_FUND_msg_gleich_sentinel_none_wird_still_verworfen);
    RUN_TEST(test_FUND_maximale_dst_und_msg_laenge_kuerzt_still_im_val_puffer);
    RUN_TEST(test_FUND_eingebettetes_nul_im_msg_kuerzt_still);

    return UNITY_END();
}
