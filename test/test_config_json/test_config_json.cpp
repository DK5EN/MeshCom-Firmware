// Native Testsuite fuer den Config-Export/-Import als JSON
// (CS-03, BACKLOG.md #3.8h; Format und kanonische CRC-Form: src/config_json.h).
//
// Geprueft wird genau das, was die Akzeptanz von CS-03 verlangt:
//   * Export -> Import stellt JEDES exportierte Feld wieder her
//     (Vergleich ueber die ganze Struktur, nicht ueber eine Handvoll Felder)
//   * falsche CRC, falsche Layout-Generation, abgeschnittene Datei und ein
//     Wert ausserhalb seines Bereichs werden abgewiesen -- und in KEINEM
//     dieser Faelle darf ein einziges Byte in meshcom_settings landen
//   * unbekannte Schluessel werden ignoriert und gezaehlt, ohne die CRC zu
//     verletzen (sonst koennte keine aeltere Firmware eine neuere Datei lesen)
//   * die kanonische Form ist genau die, die der Header beschreibt: der Test
//     baut sie unabhaengig nach und rechnet die CRC selbst
//
//   pio test -e native_config

#include <unity.h>

#include <string>
#include <cstring>

#include <Arduino.h>
#include <configuration.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/test_config_json/stubs
#include <config_json.h>
#include <crc32_util.h>

#include <ArduinoJson.h>

// ---- Link-Abhaengigkeiten von config_json.cpp ------------------------------
s_meshcom_settings meshcom_settings;
int BOARD_HARDWARE = 9;

// ---------------------------------------------------------------------------
// Helfer
// ---------------------------------------------------------------------------

static char g_buf[CONFIG_JSON_MAX];
static char g_err[160];

/* Alles auf 0 -- damit auch die Padding-Bytes definiert sind und der
 * Struktur-Vergleich im Round-Trip-Test nicht auf Muell hereinfaellt. */
static void wipe_settings(void)
{
    memset(&meshcom_settings, 0, sizeof(meshcom_settings));
}

/* Ein vollstaendig gefuellter Knoten. Die beiden Felder mit einer positiven
 * Untergrenze (max_hop_text 1..6, node_gpsbaud 1200..921600) MUESSEN gesetzt
 * werden -- ein genullter Knoten waere nicht exportierbar-importierbar. */
static void fill_settings(void)
{
    wipe_settings();

    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "DK5EN-93");
    snprintf(meshcom_settings.node_short, sizeof(meshcom_settings.node_short), "5EN93");
    meshcom_settings.node_symid = 'S';
    meshcom_settings.node_symcd = '#';
    meshcom_settings.node_lat = 48.26940877;
    meshcom_settings.node_lon = 16.40922749;
    meshcom_settings.node_lat_c = 'N';
    meshcom_settings.node_lon_c = 'E';
    meshcom_settings.node_alt = 317;

    meshcom_settings.node_temp = 21.5f;
    meshcom_settings.node_hum = 44.0f;
    meshcom_settings.node_press = 1013.25f;

    snprintf(meshcom_settings.node_ossid, sizeof(meshcom_settings.node_ossid), "meshcom-srv");
    snprintf(meshcom_settings.node_opwd, sizeof(meshcom_settings.node_opwd), "srv-secret");

    meshcom_settings.send_repeat_time = 12345;
    meshcom_settings.auto_join = true;

    meshcom_settings.node_hamnet_only = 1;
    meshcom_settings.node_sset = 0x0404;
    meshcom_settings.node_maxv = 4.24f;
    snprintf(meshcom_settings.node_extern, sizeof(meshcom_settings.node_extern), "192.168.100.100");
    meshcom_settings.node_msgid = 4711;
    meshcom_settings.node_ackid = 815;

    meshcom_settings.max_hop_text = 3;

    meshcom_settings.node_power = 14;
    meshcom_settings.node_freq = 433.175f;
    meshcom_settings.node_bw = 250.0f;
    meshcom_settings.node_sf = 11;
    meshcom_settings.node_cr = 6;

    snprintf(meshcom_settings.node_atxt, sizeof(meshcom_settings.node_atxt), "bench node \"quoted\" & \\slash");
    meshcom_settings.node_sset2 = 0x0011;
    meshcom_settings.node_owgpio = 36;
    meshcom_settings.node_temp2 = 19.75f;
    meshcom_settings.node_utcoff = 2.0f;
    meshcom_settings.node_gas_res = 12345.5f;
    meshcom_settings.node_co2 = 412.0f;

    meshcom_settings.node_mcp17io = 3;
    meshcom_settings.node_mcp17out = 5;
    meshcom_settings.node_mcp17in = 7;
    for (int i = 0; i < 16; i++)
        snprintf(meshcom_settings.node_mcp17t[i], sizeof(meshcom_settings.node_mcp17t[i]), "port%02d", i);

    for (int i = 0; i < 6; i++)
        meshcom_settings.node_gcb[i] = 100 + i;

    meshcom_settings.node_country = 8;
    meshcom_settings.node_track_freq = 433.9f;
    meshcom_settings.node_preamplebits = 32;
    meshcom_settings.node_ss_rx_pin = 12;
    meshcom_settings.node_ss_tx_pin = 13;
    meshcom_settings.node_ss_baud = 9600;
    meshcom_settings.node_postime = 30;
    snprintf(meshcom_settings.node_passwd, sizeof(meshcom_settings.node_passwd), "server-pwd");
    meshcom_settings.node_sset3 = 0x0002;
    meshcom_settings.bt_code = 123456;
    meshcom_settings.node_button_pin = 0;

    snprintf(meshcom_settings.node_ownip, sizeof(meshcom_settings.node_ownip), "192.168.68.66");
    snprintf(meshcom_settings.node_owngw, sizeof(meshcom_settings.node_owngw), "192.168.68.1");
    snprintf(meshcom_settings.node_ownms, sizeof(meshcom_settings.node_ownms), "255.255.255.0");
    snprintf(meshcom_settings.node_name, sizeof(meshcom_settings.node_name), "Martin");
    snprintf(meshcom_settings.node_webpwd, sizeof(meshcom_settings.node_webpwd), "web-secret");
    snprintf(meshcom_settings.node_ssid, sizeof(meshcom_settings.node_ssid), "home-wlan");
    snprintf(meshcom_settings.node_pwd, sizeof(meshcom_settings.node_pwd), "wlan-secret-in-clear");

    meshcom_settings.node_analog_pin = 2;
    meshcom_settings.node_analog_faktor = 1.5f;

    snprintf(meshcom_settings.node_parm, sizeof(meshcom_settings.node_parm), "PARM.A,B,C");
    snprintf(meshcom_settings.node_unit, sizeof(meshcom_settings.node_unit), "UNIT.V,A,W");
    snprintf(meshcom_settings.node_format, sizeof(meshcom_settings.node_format), "FMT.1");
    snprintf(meshcom_settings.node_eqns, sizeof(meshcom_settings.node_eqns), "EQNS.0,1,0");
    snprintf(meshcom_settings.node_values, sizeof(meshcom_settings.node_values), "1,2,3");
    meshcom_settings.node_parm_time = 15;

    meshcom_settings.node_wifi_power = 60;
    snprintf(meshcom_settings.node_lora_call, sizeof(meshcom_settings.node_lora_call), "DK5EN-1");

    meshcom_settings.node_analog_alpha = 0.25f;
    meshcom_settings.node_analog_slope = 1.0f;
    meshcom_settings.node_analog_offset = 0.5f;
    meshcom_settings.node_analog_atten = 0.125f;

    snprintf(meshcom_settings.node_gwsrv, sizeof(meshcom_settings.node_gwsrv), "AT");

    meshcom_settings.node_tempi_off = -1.5f;
    meshcom_settings.node_tempo_off = 2.25f;
    meshcom_settings.node_shunt = 0.002f;
    meshcom_settings.node_imax = 20.0f;
    meshcom_settings.node_isamp = 7;

    snprintf(meshcom_settings.node_owndns, sizeof(meshcom_settings.node_owndns), "192.168.68.1");
    meshcom_settings.node_contrast = 200;
    snprintf(meshcom_settings.node_ownntp, sizeof(meshcom_settings.node_ownntp), "ntp.example.org");

    meshcom_settings.node_gpsbaud = 9600;
    meshcom_settings.node_netmode = 1;
    meshcom_settings.node_gpsdebug = 2;
    meshcom_settings.node_relay = 0x0003;
    snprintf(meshcom_settings.node_via, sizeof(meshcom_settings.node_via), "DK5EN-99");
    meshcom_settings.node_sset4 = 0x0002;
    snprintf(meshcom_settings.node_aprsmc, sizeof(meshcom_settings.node_aprsmc), "MC");
    meshcom_settings.node_pingtime = 60;
    snprintf(meshcom_settings.node_pingcall, sizeof(meshcom_settings.node_pingcall), "OE1XAR-1");
    meshcom_settings.node_pingmax = 5;
}

static std::string do_export(void)
{
    size_t n = configExportJson(g_buf, sizeof(g_buf));
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "export did not fit into CONFIG_JSON_MAX");
    TEST_ASSERT_TRUE(n == strlen(g_buf));
    return std::string(g_buf, n);
}

static int do_import(const std::string &doc)
{
    g_err[0] = '\0';
    return configImportJson(doc.data(), doc.size(), g_err, sizeof(g_err));
}

/* Ersetzt das erste Vorkommen; bricht den Test ab, wenn es das nicht gibt --
 * ein Test, der still nichts veraendert, wuerde sonst gruen luegen. */
static std::string replace_once(const std::string &s, const std::string &from, const std::string &to)
{
    size_t p = s.find(from);
    TEST_ASSERT_TRUE_MESSAGE(p != std::string::npos, "pattern not found in exported document");
    return s.substr(0, p) + to + s.substr(p + from.size());
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/* Die CRC-Parameter selbst: der Standard-Pruefwert. Stimmt der nicht, ist
 * jede weitere Aussage dieser Suite wertlos. */
static void test_crc32_check_value(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, crc32_buf("123456789", 9));
}

/* Export -> Import stellt die komplette Struktur wieder her. */
static void test_roundtrip_restores_every_field(void)
{
    fill_settings();

    s_meshcom_settings golden;
    memcpy(&golden, &meshcom_settings, sizeof(golden));

    std::string doc = do_export();

    wipe_settings();
    TEST_ASSERT_EQUAL_INT(CFG_IMP_OK, do_import(doc));

    /* zuerst ein paar Felder namentlich -- ein memcmp-Fehler allein sagt
     * nicht, WELCHES Feld gelitten hat */
    TEST_ASSERT_EQUAL_STRING("DK5EN-93", meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_STRING("wlan-secret-in-clear", meshcom_settings.node_pwd);
    TEST_ASSERT_EQUAL_STRING("bench node \"quoted\" & \\slash", meshcom_settings.node_atxt);
    TEST_ASSERT_EQUAL_STRING("port07", meshcom_settings.node_mcp17t[7]);
    TEST_ASSERT_EQUAL_INT(3, meshcom_settings.max_hop_text);
    TEST_ASSERT_EQUAL_INT(123456, meshcom_settings.bt_code);
    TEST_ASSERT_EQUAL_UINT32(12345u, meshcom_settings.send_repeat_time);
    TEST_ASSERT_TRUE(meshcom_settings.auto_join);
    TEST_ASSERT_EQUAL_UINT32(9600u, (uint32_t)meshcom_settings.node_gpsbaud);
    /* TEST_ASSERT_EQUAL_DOUBLE ist in diesem Environment abgeschaltet
     * (Unity ohne UNITY_INCLUDE_DOUBLE) -- exakter Vergleich, der Wert ist
     * bei %.8f verlustfrei rundreisefaehig. */
    TEST_ASSERT_TRUE(meshcom_settings.node_lat == 48.26940877);
    TEST_ASSERT_TRUE(meshcom_settings.node_lon == 16.40922749);
    TEST_ASSERT_EQUAL_FLOAT(4.24f, meshcom_settings.node_maxv);
    TEST_ASSERT_EQUAL_FLOAT(0.002f, meshcom_settings.node_shunt);
    TEST_ASSERT_EQUAL_FLOAT(433.175f, meshcom_settings.node_freq);
    TEST_ASSERT_EQUAL_CHAR('N', meshcom_settings.node_lat_c);

    /* Bewusst NICHT in der Datei (config_json.cpp, Tabellenkommentar): die
     * laufenden Nachrichten-Zaehler und die letzten Sensorwerte. Sie bleiben
     * nach dem Import auf dem Wert des Knotens -- hier also auf dem Wipe. */
    TEST_ASSERT_EQUAL_INT(0, meshcom_settings.node_msgid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, meshcom_settings.node_temp);
    golden.node_msgid = meshcom_settings.node_msgid;
    golden.node_ackid = meshcom_settings.node_ackid;
    golden.node_temp = meshcom_settings.node_temp;
    golden.node_hum = meshcom_settings.node_hum;
    golden.node_press = meshcom_settings.node_press;
    golden.node_temp2 = meshcom_settings.node_temp2;
    golden.node_gas_res = meshcom_settings.node_gas_res;
    golden.node_co2 = meshcom_settings.node_co2;

    /* und dann die ganze Struktur, damit kein Feld unbemerkt durchrutscht */
    TEST_ASSERT_EQUAL_MEMORY(&golden, &meshcom_settings, sizeof(golden));
}

/* Ein zweiter Export nach dem Import muss byteidentisch sein -- das ist die
 * Akzeptanzbedingung aus dem Backlog. */
static void test_reexport_is_identical(void)
{
    fill_settings();
    std::string first = do_export();

    wipe_settings();
    TEST_ASSERT_EQUAL_INT(CFG_IMP_OK, do_import(first));

    std::string second = do_export();
    TEST_ASSERT_EQUAL_STRING(first.c_str(), second.c_str());
}

/* Die CRC haengt an den WERTEN, nicht an den Bytes: dieselbe Datei neu
 * formatiert (Einrueckung, andere Zahlenschreibweise) muss weiter passen. */
static void test_crc_survives_reformatting(void)
{
    fill_settings();
    std::string doc = do_export();

    JsonDocument parsed;
    TEST_ASSERT_FALSE(deserializeJson(parsed, doc));

    std::string pretty;
    serializeJsonPretty(parsed, pretty);
    TEST_ASSERT_TRUE(pretty != doc);

    wipe_settings();
    TEST_ASSERT_EQUAL_INT(CFG_IMP_OK, do_import(pretty));
    TEST_ASSERT_EQUAL_STRING("DK5EN-93", meshcom_settings.node_call);
}

/* Die kanonische Form unabhaengig nachgebaut (config_json.h, "CANONICAL
 * FORM"): Kopfzeile, layout/fw/hw, dann die vorhandenen Felder in
 * TABELLENreihenfolge -- hier bewusst gegen die Reihenfolge in der Datei,
 * in der node_alt vor node_call steht. */
static void test_canonical_form_is_as_documented(void)
{
    wipe_settings();
    meshcom_settings.max_hop_text = 4;      /* damit die Struktur exportierbar bleibt */
    meshcom_settings.node_gpsbaud = 38400;

    char fw[24];
    snprintf(fw, sizeof(fw), "%s%s", SOURCE_VERSION, SOURCE_VERSION_SUB);

    char head[128];
    snprintf(head, sizeof(head), "MC-CFG-1\nlayout=%d\nfw=%s\nhw=%d\n",
             (int)FLASH_STRUCT_VERSION, fw, BOARD_HARDWARE);

    std::string canonical = head;
    canonical += "node_call=TEST-1\n";      /* node_call steht in der Tabelle vor ... */
    canonical += "node_alt=123\n";          /* ... node_alt */

    char crc[16];
    snprintf(crc, sizeof(crc), "%08x",
             (unsigned int)crc32_buf(canonical.data(), canonical.size()));

    char doc[512];
    snprintf(doc, sizeof(doc),
             "{\"meshcom_config\":{\"layout\":%d,\"fw\":\"%s\",\"hw\":%d,"
             "\"settings\":{\"node_alt\":\"123\",\"node_call\":\"TEST-1\"},"
             "\"crc32\":\"%s\"}}",
             (int)FLASH_STRUCT_VERSION, fw, BOARD_HARDWARE, crc);

    TEST_ASSERT_EQUAL_INT(CFG_IMP_OK, do_import(std::string(doc)));
    TEST_ASSERT_EQUAL_STRING("TEST-1", meshcom_settings.node_call);
    TEST_ASSERT_EQUAL_INT(123, meshcom_settings.node_alt);
    /* fehlende Schluessel behalten den bisherigen Wert */
    TEST_ASSERT_EQUAL_INT(4, meshcom_settings.max_hop_text);
}

static void test_crc_mismatch_is_refused(void)
{
    fill_settings();
    std::string doc = do_export();

    /* nur die acht Hex-Ziffern austauschen, sonst bleibt die Datei identisch */
    std::string bad = doc;
    size_t p = bad.rfind("\"crc32\":\"");
    TEST_ASSERT_TRUE(p != std::string::npos);
    bad.replace(p + 9, 8, "deadbeef");
    TEST_ASSERT_TRUE(bad != doc);

    wipe_settings();
    s_meshcom_settings before;
    memcpy(&before, &meshcom_settings, sizeof(before));

    TEST_ASSERT_EQUAL_INT(CFG_IMP_ECRC, do_import(bad));
    TEST_ASSERT_EQUAL_MEMORY(&before, &meshcom_settings, sizeof(before));
}

static void test_layout_mismatch_is_refused(void)
{
    fill_settings();
    std::string doc = do_export();

    char from[48];
    snprintf(from, sizeof(from), "\"layout\":%d", (int)FLASH_STRUCT_VERSION);
    std::string bad = replace_once(doc, from, "\"layout\":19700101");

    wipe_settings();
    s_meshcom_settings before;
    memcpy(&before, &meshcom_settings, sizeof(before));

    TEST_ASSERT_EQUAL_INT(CFG_IMP_ELAYOUT, do_import(bad));
    TEST_ASSERT_EQUAL_MEMORY(&before, &meshcom_settings, sizeof(before));
}

static void test_truncated_json_is_refused(void)
{
    fill_settings();
    std::string doc = do_export();
    std::string bad = doc.substr(0, doc.size() / 2);

    wipe_settings();
    s_meshcom_settings before;
    memcpy(&before, &meshcom_settings, sizeof(before));

    TEST_ASSERT_EQUAL_INT(CFG_IMP_EPARSE, do_import(bad));
    TEST_ASSERT_EQUAL_MEMORY(&before, &meshcom_settings, sizeof(before));
}

/* Unbekannte Schluessel (Datei einer neueren Firmware) werden ignoriert und
 * gezaehlt -- und duerfen die CRC nicht kippen. */
static void test_unknown_key_is_ignored_and_counted(void)
{
    fill_settings();
    std::string doc = do_export();
    std::string mod = replace_once(doc, "\"settings\":{",
                                        "\"settings\":{\"node_futurefield\":\"42\",\"node_alsonew\":\"x\",");

    wipe_settings();
    TEST_ASSERT_EQUAL_INT(CFG_IMP_OK, do_import(mod));
    TEST_ASSERT_EQUAL_STRING("DK5EN-93", meshcom_settings.node_call);
    TEST_ASSERT_NOT_NULL(strstr(g_err, "2 unknown"));
}

/* Ein Wert ausserhalb seines Bereichs weist die GANZE Datei ab -- keine
 * halb angewandte Konfiguration. */
static void test_out_of_range_value_is_refused(void)
{
    fill_settings();
    std::string doc = do_export();
    std::string bad = replace_once(doc, "\"max_hop_text\":\"3\"", "\"max_hop_text\":\"9\"");

    wipe_settings();
    s_meshcom_settings before;
    memcpy(&before, &meshcom_settings, sizeof(before));

    TEST_ASSERT_EQUAL_INT(CFG_IMP_EVALUE, do_import(bad));
    TEST_ASSERT_NOT_NULL(strstr(g_err, "max_hop_text"));
    TEST_ASSERT_EQUAL_MEMORY(&before, &meshcom_settings, sizeof(before));
}

/* Zu langer String fuer sein Zielfeld: dieselbe Abweisung, kein Ueberlauf. */
static void test_oversized_string_is_refused(void)
{
    fill_settings();
    std::string doc = do_export();
    std::string bad = replace_once(doc, "\"node_call\":\"DK5EN-93\"",
                                        "\"node_call\":\"DK5EN-93-MUCH-TOO-LONG\"");

    wipe_settings();
    s_meshcom_settings before;
    memcpy(&before, &meshcom_settings, sizeof(before));

    TEST_ASSERT_EQUAL_INT(CFG_IMP_EVALUE, do_import(bad));
    TEST_ASSERT_EQUAL_MEMORY(&before, &meshcom_settings, sizeof(before));
}

/* TX-Power-Sentinel -20 ("noch nichts gespeichert") liegt ausserhalb des
 * TX_POWER_MIN..MAX-Fensters mancher Boards und muss trotzdem durchgehen --
 * sonst kann ein fabrikfrischer Knoten seinen eigenen Export nicht lesen. */
static void test_power_sentinel_is_accepted(void)
{
    fill_settings();
    meshcom_settings.node_power = -20;
    std::string doc = do_export();

    wipe_settings();
    TEST_ASSERT_EQUAL_INT(CFG_IMP_OK, do_import(doc));
    TEST_ASSERT_EQUAL_INT(-20, meshcom_settings.node_power);
}

static void test_empty_and_oversized_input_is_refused(void)
{
    wipe_settings();
    TEST_ASSERT_EQUAL_INT(CFG_IMP_EARG, configImportJson(NULL, 0, g_err, sizeof(g_err)));

    std::string huge(CONFIG_JSON_MAX + 1, 'x');
    TEST_ASSERT_EQUAL_INT(CFG_IMP_EARG, do_import(huge));
}

/* Ein zu kleiner Puffer liefert 0 und einen leeren String, statt zu schreiben,
 * was gerade noch passte. */
static void test_export_into_small_buffer_fails_cleanly(void)
{
    fill_settings();

    char small[64];
    memset(small, 'A', sizeof(small));
    TEST_ASSERT_TRUE(configExportJson(small, sizeof(small)) == 0);
    TEST_ASSERT_EQUAL_STRING("", small);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_roundtrip_restores_every_field);
    RUN_TEST(test_reexport_is_identical);
    RUN_TEST(test_crc_survives_reformatting);
    RUN_TEST(test_canonical_form_is_as_documented);
    RUN_TEST(test_crc_mismatch_is_refused);
    RUN_TEST(test_layout_mismatch_is_refused);
    RUN_TEST(test_truncated_json_is_refused);
    RUN_TEST(test_unknown_key_is_ignored_and_counted);
    RUN_TEST(test_out_of_range_value_is_refused);
    RUN_TEST(test_oversized_string_is_refused);
    RUN_TEST(test_power_sentinel_is_accepted);
    RUN_TEST(test_empty_and_oversized_input_is_refused);
    RUN_TEST(test_export_into_small_buffer_fails_cleanly);

    return UNITY_END();
}
