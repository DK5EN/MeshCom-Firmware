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

// BP-09: sendMessage() now returns int (BpSendResult, backpressure.h); this
// stub always reports success since getExtern()'s "msg" path (extudp_functions.cpp)
// discards the return value anyway (E4: what doesn't go out on HF doesn't go
// into the backbone either, but that decision is made inside sendMessage()
// itself, not by this caller).
int sendMessage(char *msg_text, int len)
{
    g_sent_msg_text.assign(msg_text, msg_text + len);
    g_sent_msg_len = len;
    g_sendMessage_calls++;
    return BP_SEND_OK;
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

// ------------------------------------- TM-43: Grenzwerte und Transportgroessen
// Die sechs Ablehnungsvektoren aus BACKLOG #3.8l (TM-43) laufen am Bench als
// UDP-Datagramme gegen die echte Node (tools/bench/extudp_peer.py,
// rejection_vectors()); hier stehen die, die sich nativ am Parser pruefen
// lassen -- gleiche Nutzlasten, ohne Hardwarezeit.

static bool serialContains(const char *needle)
{
    return Serial.captured().find(needle) != std::string::npos;
}

static void test_dst_grenze_9_zeichen_wird_akzeptiert(void)
{
    // Genau AUF der Schranke (strlen(dst) > 9 verwirft): 9 Zeichen muessen
    // durch. Der 10-Zeichen-Fall darueber ist bereits abgedeckt -- dieses
    // Paar pinnt die Grenze von beiden Seiten, damit ein "> 9" -> ">= 9"
    // Vertipper im Firmware-Code nicht unbemerkt bleibt.
    callGetExtern(R"({"type":"msg","dst":"123456789","msg":"an der Grenze"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING(":{123456789}an der Grenze", g_sent_msg_text.c_str());
}

static void test_dst_grenze_10_zeichen_wird_verworfen_und_geloggt(void)
{
    callGetExtern(R"({"type":"msg","dst":"1234567890","msg":"eins zuviel"})");

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    // TM-43: jeder Ablehnungsfall MUSS eine Spur auf der Konsole hinterlassen
    // -- am Bench ist genau diese Zeile der Beweis, dass die Node das
    // Datagramm gesehen und bewusst verworfen hat (statt zu sterben).
    TEST_ASSERT_TRUE(serialContains("[EXT] invalid lengths"));
}

static void test_msg_grenze_150_zeichen_wird_akzeptiert(void)
{
    // 150 Zeichen mit kurzem dst: der Rahmen ":{A}" + 150 = 154 Zeichen passt
    // vollstaendig in val[161] -- keine stille Kuerzung (anders als FUND 2,
    // das dst UND msg gleichzeitig auf Maximum setzt).
    std::string msg(150, 'M');
    callGetExtern(std::string(R"({"type":"msg","dst":"A","msg":")") + msg + R"("})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_INT((int)(4 + 150), g_sent_msg_len);
    TEST_ASSERT_EQUAL_STRING((":{A}" + msg).c_str(), g_sent_msg_text.c_str());
}

static void test_msg_grenze_151_zeichen_wird_verworfen_und_geloggt(void)
{
    std::string msg(151, 'M');
    callGetExtern(std::string(R"({"type":"msg","dst":"A","msg":")") + msg + R"("})");

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_TRUE(serialContains("[EXT] invalid lengths"));
}

static void test_volles_255_byte_datagramm_wird_wie_die_node_auf_254_gekuerzt(void)
{
    // Transportgrenze: getExternUDP() liest hoechstens UDP_TX_BUF_SIZE-1 = 254
    // Bytes (extudp_functions.cpp: UdpExtern.read(incomingExtPacket,
    // UDP_TX_BUF_SIZE - 1)) und setzt danach incomingExtPacket[len] = 0. Ein
    // Datagramm von exakt 255 B -- der Vektor full_255_byte_datagram in
    // tools/bench/extudp_peer.py -- verliert also sein letztes Byte, hier die
    // schliessende Klammer. Erwartung: sauberer Parserfehler mit Logzeile,
    // kein Ueberlauf, kein Absturz.
    std::string head = R"({"type":"msg","dst":"TEST","msg":")";
    std::string tail = R"("})";
    std::string full = head + std::string(255 - head.size() - tail.size(), 'F') + tail;
    TEST_ASSERT_EQUAL_INT(255, (int)full.size());

    callGetExtern(full, 254);      // genau so viel, wie die Node lesen wuerde

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_TRUE(serialContains("deserializeJson() failed"));
}

static void test_volles_254_byte_datagramm_wird_vollstaendig_verarbeitet(void)
{
    // Gegenprobe zum Fall darueber: 254 B sind die groesste Nutzlast, die
    // ungekuerzt bei getExtern() ankommt. Mit einer msg innerhalb der
    // 150-Zeichen-Schranke (Rest als ignoriertes Zusatzfeld) muss sie
    // vollstaendig verarbeitet werden -- die Groesse allein darf nichts
    // verwerfen.
    std::string msg(150, 'M');
    std::string head = std::string(R"({"type":"msg","dst":"TEST","msg":")") + msg
                       + R"(","pad":")";
    std::string tail = R"("})";
    std::string full = head + std::string(254 - head.size() - tail.size(), 'P') + tail;
    TEST_ASSERT_EQUAL_INT(254, (int)full.size());

    callGetExtern(full, 254);

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING((":{TEST}" + msg).c_str(), g_sent_msg_text.c_str());
}

static void test_mitten_im_json_abgeschnittenes_datagramm_wird_verworfen_und_geloggt(void)
{
    // TM-43-Vektor truncated_mid_json: die erste Haelfte eines gueltigen
    // Rahmens, wie ihn ein abgerissener Sender schicken wuerde. Anders als
    // test_len_kuerzer_als_puffer... traegt der Puffer hier auch physisch
    // nichts mehr hinter len -- der Parser darf trotzdem nicht ueber das
    // Pufferende hinauslaufen.
    std::string full = R"({"type":"msg","dst":"TEST","msg":"truncated case"})";
    std::string half = full.substr(0, full.size() / 2);

    callGetExtern(half);

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_INT(0, g_sendPosition_calls);
    TEST_ASSERT_TRUE(serialContains("deserializeJson() failed"));
}

static void test_fehlendes_dst_und_fehlendes_msg_werden_geloggt(void)
{
    // Die beiden uebrigen TM-43-Ablehnungsvektoren: das Verwerfen ist oben
    // schon gepinnt, hier zusaetzlich die Logspur, auf die die Bench-Probe
    // pro Vektor wartet.
    callGetExtern(R"({"type":"msg","msg":"kein dst"})");
    TEST_ASSERT_TRUE(serialContains("[EXT] missing dst/msg"));

    Serial.clear();
    callGetExtern(R"({"type":"msg","dst":"TEST"})");
    TEST_ASSERT_TRUE(serialContains("[EXT] missing dst/msg"));

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
// PT-01-Funde 4-6 -- in getExtern() behoben, hier als echte Assertions
// gepinnt (vor dem Fix waren es TEST_IGNORE_MESSAGE-Faelle).
// ====================================================================

// FUND 4 -- Sentinel-Kollision, BEHOBEN: aprsmsg.msg_payload wurde mit dem
// Literal "none" als interner "nichts gesetzt"-Marker vorbelegt, den eine
// spaetere Pruefung `if(aprsmsg.msg_payload == "none") return;` wieder
// auslas. Eine legitime Nachricht mit genau dem Text "none" kollidierte
// damit und wurde wortlos verworfen. Die Anwesenheit entscheidet jetzt
// allein das JSON (fehlender Schluessel -> null-Variant -> Nullzeiger),
// "none" ist damit gewoehnlicher Text und muss wie jeder andere raus.
static void test_msg_gleich_none_ist_gueltiger_text_und_wird_gesendet(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE5BYE-1","msg":"none"})");

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_STRING(":{OE5BYE-1}none", g_sent_msg_text.c_str());
    TEST_ASSERT_EQUAL_INT((int)strlen(":{OE5BYE-1}none"), g_sent_msg_len);
}

// FUND 5 -- Stille Kuerzung bei kombinierter Maximallaenge, BEHOBEN: dst
// darf bis zu 9 Zeichen, msg bis zu 150 Zeichen lang sein (je einzeln
// geprueft), der Rahmen ":{dst}msg" braucht bei beiden Maxima aber
// 2+9+1+150 = 162 Zeichen -- das alte `snprintf(val, 160, ...)` in einen
// char val[161] konnte davon nur 159 schreiben und liess die letzten 3
// Zeichen ersatzlos verschwinden. val ist jetzt auf das echte Maximum
// dimensioniert (2+9+1+150+NUL = 163) und snprintf durch sizeof(val)
// begrenzt: der Rahmen muss vollstaendig bei sendMessage() ankommen.
static void test_maximale_dst_und_msg_laenge_kommt_vollstaendig_an(void)
{
    std::string dst(9, '1');            // an der 9-Zeichen-Schranke
    std::string msg(150, 'M');          // an der 150-Zeichen-Schranke
    callGetExtern(std::string(R"({"type":"msg","dst":")") + dst + R"(","msg":")" + msg + R"("})");

    const std::string expected = ":{" + dst + "}" + msg;
    TEST_ASSERT_EQUAL_INT(162, (int)expected.size());   // 2 + 9 + 1 + 150

    TEST_ASSERT_EQUAL_INT(1, g_sendMessage_calls);
    TEST_ASSERT_EQUAL_INT(162, g_sent_msg_len);         // nichts unterwegs verloren
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), g_sent_msg_text.c_str());
}

// FUND 6 -- Eingebettetes NUL im msg-Feld, BEHOBEN: \u0000 innerhalb der
// JSON-Payload dekodiert zu einem rohen NUL-Byte, aber inputJson["msg"]
// liefert einen C-String, und jede nachfolgende Verarbeitung (strlen() fuer
// die Laengenpruefung, die Arduino-String-Zuweisung, das abschliessende
// snprintf("%s", ...)) terminiert an genau diesem Byte -- alles dahinter
// wurde kommentarlos verschluckt. Ein NUL ueberlebt diese Kette nicht, also
// wird das Datagramm jetzt komplett abgelehnt: die rohe JSON-Laenge
// (JsonString::size()) wird gegen strlen() geprueft, eine Abweichung wird
// verworfen und geloggt wie jeder andere Ablehnungsvektor.
static void test_eingebettetes_nul_im_msg_wird_verworfen_und_geloggt(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE5BYE-1","msg":"ab\u0000cd"})");

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);   // abgelehnt, NICHT still gekuerzt
    TEST_ASSERT_TRUE(serialContains("[EXT] NUL in payload"));
}

// Dieselbe Pruefung auf dem anderen Feld: ein NUL in dst wuerde das
// Zielrufzeichen still verkuerzen (":{OE}" statt ":{OE5BYE-1}") und die
// Nachricht fehlleiten -- faellt auf genau demselben Test heraus.
static void test_eingebettetes_nul_im_dst_wird_verworfen_und_geloggt(void)
{
    callGetExtern(R"({"type":"msg","dst":"OE\u0000BYE-1","msg":"text"})");

    TEST_ASSERT_EQUAL_INT(0, g_sendMessage_calls);
    TEST_ASSERT_TRUE(serialContains("[EXT] NUL in payload"));
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

    // TM-43: Grenzwerte beidseitig und die Transportgroessen des Datagramms
    RUN_TEST(test_dst_grenze_9_zeichen_wird_akzeptiert);
    RUN_TEST(test_dst_grenze_10_zeichen_wird_verworfen_und_geloggt);
    RUN_TEST(test_msg_grenze_150_zeichen_wird_akzeptiert);
    RUN_TEST(test_msg_grenze_151_zeichen_wird_verworfen_und_geloggt);
    RUN_TEST(test_volles_255_byte_datagramm_wird_wie_die_node_auf_254_gekuerzt);
    RUN_TEST(test_volles_254_byte_datagramm_wird_vollstaendig_verarbeitet);
    RUN_TEST(test_mitten_im_json_abgeschnittenes_datagramm_wird_verworfen_und_geloggt);
    RUN_TEST(test_fehlendes_dst_und_fehlendes_msg_werden_geloggt);

    RUN_TEST(test_len_kuerzer_als_puffer_bricht_den_parse_sauber_ab);
    RUN_TEST(test_len_ignoriert_bytes_hinter_len);

    RUN_TEST(test_verschachtelte_zusatzschluessel_werden_ignoriert);

    RUN_TEST(test_tele_numerisches_feld_als_json_string);

    RUN_TEST(test_msg_gleich_none_ist_gueltiger_text_und_wird_gesendet);
    RUN_TEST(test_maximale_dst_und_msg_laenge_kommt_vollstaendig_an);
    RUN_TEST(test_eingebettetes_nul_im_msg_wird_verworfen_und_geloggt);
    RUN_TEST(test_eingebettetes_nul_im_dst_wird_verworfen_und_geloggt);

    return UNITY_END();
}
