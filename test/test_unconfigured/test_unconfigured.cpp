// Native Testsuite fuer RX-01/TX-01 (BACKLOG 3.8k, Operator-Intake
// 2026-08-30): ein Knoten, der noch das Werksrufzeichen traegt, darf weder
// gesendet noch weitergeleitet werden.
//
// Diese Suite deckt das nativ Pruefbare ab:
//
//   * isUnconfiguredCall() (configuration_global.h) -- reines Praedikat,
//     das RX-01 (Drop-Punkt in lora_functions.cpp/udp_functions.cpp) und
//     TX-01 (Guard in addTxRingEntry()/doTX()) gemeinsam benutzen.
//   * makeDhcpHostname() (configuration_global.h) -- baut aus demselben
//     Praedikat (isUnconfiguredCall statt isNodeUnconfigured, siehe dortiger
//     Kommentar) einen DHCP-Option-12-Hostnamen aus dem Rufzeichen. Kein
//     RX-01/TX-01-Guard selbst, aber derselbe Header, dieselbe Vorbedingung,
//     dieselbe Suite.
//   * addTxRingEntry() (txring_functions.cpp) -- der TX-01-Ring-Guard: -1
//     wenn meshcom_settings.node_call unkonfiguriert ist, normales Enqueue
//     sonst. Inklusive Zaehler (stat_tx_refuse_unconfigured) und der
//     10s-Rate-Limitierung der Refuse-Marker-Zeile.
//
// doTX() (lora_functions.cpp) ist NICHT Teil dieser Suite: die Funktion
// haengt an RadioLib/den board-spezifischen TX-Zweigen und ist nativ nicht
// uebersetzbar. Der Ring-Guard hier plus das Praedikat sind das Minimum, das
// nativ getestet werden kann -- der Hardware-Backstop in doTX() (identischer
// Guard, siehe dortiger Kommentar) bleibt auf den Bank-Check angewiesen.
//
//   pio test -e native_aprs -f test_unconfigured

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <txring_functions.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
// (identischer Stub-Satz wie test_txring.cpp/test_txring_flood.cpp -- jede
// native_aprs-Testsuite linkt aprs_functions.cpp mit ein, siehe
// build_src_filter in platformio.ini)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

// ---- Globals, die txring_functions.cpp per extern erwartet -----------------
// Definiert in txring_functions.cpp selbst (NATIVE_BUILD-Zweig), siehe
// dortigen Kommentar bzw. test_txring.cpp.

static void resetRing(void)
{
    memset(ringBuffer, 0, sizeof(ringBuffer));
    iWrite = 0;
    iRead = 0;
    memset(retryCount, 0, sizeof(retryCount));
    memset(ringPriority, 0, sizeof(ringPriority));
    memset(ringEnqueueTime, 0, sizeof(ringEnqueueTime));
    memset(stat_drop_count, 0, sizeof(stat_drop_count));
    stat_queue_hwm = 0;
    mc_test_set_millis(0);
}

static void setNodeCall(const char *call)
{
    memset(meshcom_settings.node_call, 0, sizeof(meshcom_settings.node_call));
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", call);
}

void setUp(void)
{
    resetRing();
    setNodeCall("DK5EN-90"); // konfiguriert, sofern ein Test nichts anderes setzt
    Serial.clear();
}
void tearDown(void) {}

// --------------------------------------------------------------- Frame-Bau
// (reduzierter Ausschnitt von test_txring.cpp -- hier reicht ein
// Positionsframe, die Prioritaets-Klassifizierung selbst ist nicht Gegenstand
// dieser Suite)

struct BuiltFrame
{
    uint8_t bytes[64];
    uint16_t len;
};

static BuiltFrame buildPositionFrame(uint32_t id = 0x11223344UL)
{
    static const uint8_t payload[] = "4825.35N\\01147.19E-Test";
    BuiltFrame f{};
    uint16_t n = 0;
    f.bytes[n++] = MSG_TYPE_POSITION;
    f.bytes[n++] = (uint8_t)(id & 0xFF);
    f.bytes[n++] = (uint8_t)((id >> 8) & 0xFF);
    f.bytes[n++] = (uint8_t)((id >> 16) & 0xFF);
    f.bytes[n++] = (uint8_t)((id >> 24) & 0xFF);
    f.bytes[n++] = 0x12; // byte5 (Flags/Hop), Inhalt hier irrelevant
    memcpy(f.bytes + n, payload, sizeof(payload) - 1);
    n = (uint16_t)(n + sizeof(payload) - 1);
    f.bytes[n++] = 0x00; // Terminator
    f.len = n;
    return f;
}

// ------------------------------------------------- Test 1: isUnconfiguredCall

static void test_isUnconfiguredCall_predicate(void)
{
    // Werkseinstellung, mit und ohne SSID -- der Praefixtest deckt beides ab.
    TEST_ASSERT_TRUE(isUnconfiguredCall("XX0XXX-00"));
    TEST_ASSERT_TRUE(isUnconfiguredCall("XX0XXX-1"));
    TEST_ASSERT_TRUE(isUnconfiguredCall("XX0XXX"));

    // Leer / "none" / nullptr.
    TEST_ASSERT_TRUE(isUnconfiguredCall(""));
    TEST_ASSERT_TRUE(isUnconfiguredCall("none"));
    TEST_ASSERT_TRUE(isUnconfiguredCall(nullptr));

    // Ein echtes Rufzeichen ist NICHT unkonfiguriert.
    TEST_ASSERT_FALSE(isUnconfiguredCall("DK5EN-14"));
    TEST_ASSERT_FALSE(isUnconfiguredCall("DL2JA-2"));
    TEST_ASSERT_FALSE(isUnconfiguredCall("OE1XYZ"));

    // Gross-/Kleinschreibung: der Vergleich ist bewusst case-sensitiv (echte
    // Rufzeichen sind immer Grossbuchstaben) -- ein kleingeschriebenes
    // Pseudo-Praefix darf NICHT als unkonfiguriert durchgehen, sonst
    // erweitert sich die Erkennung stillschweigend ueber ihre Regel hinaus.
    TEST_ASSERT_FALSE(isUnconfiguredCall("xx0xxx-00"));
    TEST_ASSERT_FALSE(isUnconfiguredCall("None"));
}

// ------------------------------------------------- Test 1b: makeDhcpHostname

// Fuellt buf komplett mit einem Sentinel-Byte, ruft makeDhcpHostname() mit
// einem (laut isUnconfiguredCall()) unkonfigurierten Rufzeichen auf und prueft
// beides: Rueckgabe false UND Puffer komplett unangetastet -- der fruehe
// return in makeDhcpHostname() (out==nullptr || n<2 || isUnconfiguredCall())
// schreibt kein einziges Byte, bevor er zurueckkehrt.
static void assertRefusedAndUntouched(const char *call)
{
    char buf[8];
    memset(buf, 0xAA, sizeof(buf));
    char before[8];
    memcpy(before, buf, sizeof(buf));

    TEST_ASSERT_FALSE(makeDhcpHostname(buf, sizeof(buf), call));
    TEST_ASSERT_EQUAL_UINT8_ARRAY((uint8_t *)before, (uint8_t *)buf, sizeof(buf));
}

static void test_makeDhcpHostname_configured_call_roundtrip(void)
{
    char buf[32];
    memset(buf, 0xAA, sizeof(buf));

    TEST_ASSERT_TRUE(makeDhcpHostname(buf, sizeof(buf), "DK5EN-93"));
    TEST_ASSERT_EQUAL_STRING("DK5EN-93", buf);
}

// makeDhcpHostname() hat keinen Gross-/Kleinschreibungs-Schritt -- der
// Zeichenfilter laesst a-z genauso durch wie A-Z (siehe "ok"-Bedingung im
// Helfer). Kleingeschriebene Eingabe kommt also unveraendert zurueck.
static void test_makeDhcpHostname_lowercase_not_uppercased(void)
{
    char buf[32];
    memset(buf, 0xAA, sizeof(buf));

    TEST_ASSERT_TRUE(makeDhcpHostname(buf, sizeof(buf), "dk5en-93"));
    TEST_ASSERT_EQUAL_STRING("dk5en-93", buf);
}

static void test_makeDhcpHostname_refuses_unconfigured_and_leaves_buffer_untouched(void)
{
    assertRefusedAndUntouched(DEFAULT_CALL);        // "XX0XXX-00"
    assertRefusedAndUntouched(DEFAULT_CALL_PREFIX); // "XX0XXX", ohne SSID
    assertRefusedAndUntouched("");
    assertRefusedAndUntouched("none");
    assertRefusedAndUntouched(nullptr);
}

// '/' und '_' sind kein Bestandteil eines Rufzeichens (checkRegexCall()
// filtert das im Normalfall schon vorher aus) -- die Rueckfallebene in
// makeDhcpHostname() bildet beide auf '-' ab.
static void test_makeDhcpHostname_illegal_chars_mapped_to_hyphen(void)
{
    char buf[32];
    memset(buf, 0xAA, sizeof(buf));

    TEST_ASSERT_TRUE(makeDhcpHostname(buf, sizeof(buf), "DK5EN/9_3"));
    TEST_ASSERT_EQUAL_STRING("DK5EN-9-3", buf);
}

// Zeichen, die auf '-' abgebildet werden UND am Ende landen, werden per
// RFC 1123 (Label endet alphanumerisch) wieder entfernt -- hier durch zwei
// illegale Endzeichen ('/' und '_') erzeugt, nicht durch woertliche '-'.
static void test_makeDhcpHostname_trailing_hyphens_trimmed(void)
{
    char buf[32];
    memset(buf, 0xAA, sizeof(buf));

    TEST_ASSERT_TRUE(makeDhcpHostname(buf, sizeof(buf), "DK5EN-93/_"));
    TEST_ASSERT_EQUAL_STRING("DK5EN-93", buf);
}

// Interessanter Randfall: eine Eingabe, die NUR aus illegalen Zeichen
// besteht. Die Schleife schreibt sie alle als '-' (o=3), der Trim-Schritt
// laeuft danach bis o==0 zurueck (alle drei sind '-'), also gibt der Helfer
// false zurueck (o>0 ist falsch). ABER: anders als beim fruehen return oben
// ist der Puffer NICHT unangetastet -- out[0..2] wurden von der Schleife
// bereits mit '-' beschrieben, nur out[0] wird danach zusaetzlich auf NUL
// gesetzt (die Terminierung liegt bei Index o, hier 0). out[1]/out[2] bleiben
// als '-' stehen, der Rest des Puffers bleibt Sentinel. Das ist aus Helfer-
// Sicht unschaedlich (Rueckgabewert false, Aufrufer darf out bei false
// ohnehin nicht benutzen), aber eben kein "Puffer unveraendert" wie bei den
// fruehen returns -- bewusst so mitgetestet statt angenommen.
static void test_makeDhcpHostname_all_illegal_chars_returns_false(void)
{
    char buf[8];
    memset(buf, 0xAA, sizeof(buf));

    TEST_ASSERT_FALSE(makeDhcpHostname(buf, sizeof(buf), "___"));
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)buf[0]);          // Terminator bei o==0
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'-', (uint8_t)buf[1]); // von der Schleife geschrieben, vom Trim nicht geloescht
    TEST_ASSERT_EQUAL_UINT8((uint8_t)'-', (uint8_t)buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)buf[3]);       // ab hier: von der Schleife nie erreicht
}

// Eingabe laenger als der Puffer: die Schleifenbedingung "o < n-1" schneidet
// bei n-1 Zeichen ab, danach folgt IMMER "out[o]=0" -- bei n=8 landet der
// Terminator also exakt auf dem letzten Byte des Puffers, nie dahinter.
// Guard-Bytes vor und hinter dem eigentlichen Puffer decken einen Overrun in
// beide Richtungen ab.
static void test_makeDhcpHostname_truncates_to_buffer_and_never_overruns(void)
{
    char storage[14];
    memset(storage, 0xAA, sizeof(storage));
    char *out = storage + 3; // storage[0..2] und storage[11..13] sind Guard-Zonen
    unsigned long n = 8;

    TEST_ASSERT_TRUE(makeDhcpHostname(out, n, "DK5ENALPHA99"));
    TEST_ASSERT_EQUAL_STRING("DK5ENAL", out); // erste n-1 = 7 Zeichen, kein Trim noetig

    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)storage[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)storage[1]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)storage[2]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)storage[11]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)storage[12]);
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)storage[13]);
}

// n<2 (und out==nullptr) sind die beiden anderen fruehen returns -- keiner
// von ihnen erreicht die Schleife, der Puffer bleibt unangetastet.
static void test_makeDhcpHostname_guard_clauses_write_nothing(void)
{
    TEST_ASSERT_FALSE(makeDhcpHostname(nullptr, 8, "DK5EN-93"));

    char buf[4];
    memset(buf, 0xAA, sizeof(buf));
    TEST_ASSERT_FALSE(makeDhcpHostname(buf, 1, "DK5EN-93"));
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)buf[0]);

    TEST_ASSERT_FALSE(makeDhcpHostname(buf, 0, "DK5EN-93"));
    TEST_ASSERT_EQUAL_UINT8(0xAA, (uint8_t)buf[0]);
}

// ------------------------------------------- Test 2: addTxRingEntry()-Guard

static void test_addTxRingEntry_refuses_when_node_unconfigured(void)
{
    setNodeCall("XX0XXX-00");
    uint32_t before = stat_tx_refuse_unconfigured;

    BuiltFrame f = buildPositionFrame();
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t-unconf");

    TEST_ASSERT_EQUAL_INT(-1, slot);
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iWrite);  // Ring unangetastet
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)iRead);
    TEST_ASSERT_EQUAL_UINT8(0, ringBuffer[0][0]);
    TEST_ASSERT_EQUAL_UINT32(before + 1, stat_tx_refuse_unconfigured);
}

static void test_addTxRingEntry_refuses_for_empty_and_none(void)
{
    setNodeCall("");
    BuiltFrame f1 = buildPositionFrame();
    TEST_ASSERT_EQUAL_INT(-1, addTxRingEntry(f1.bytes, f1.len, RING_STATUS_READY, "t-empty"));

    setNodeCall("none");
    BuiltFrame f2 = buildPositionFrame();
    TEST_ASSERT_EQUAL_INT(-1, addTxRingEntry(f2.bytes, f2.len, RING_STATUS_READY, "t-none"));
}

static void test_addTxRingEntry_queues_when_node_configured(void)
{
    setNodeCall("DK5EN-90");
    uint32_t before = stat_tx_refuse_unconfigured;

    BuiltFrame f = buildPositionFrame();
    int slot = addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "t-conf");

    TEST_ASSERT_EQUAL_INT(0, slot);
    TEST_ASSERT_EQUAL_UINT8(1, (uint8_t)iWrite);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(f.bytes, &ringBuffer[slot][2], f.len);
    TEST_ASSERT_EQUAL_UINT32(before, stat_tx_refuse_unconfigured); // unveraendert
}

// A second, immediately following enqueue must also succeed normally --
// TX-01 refuses whole-node, not per-call, so a configured node is not left
// in some latched refuse-state by an earlier check.
static void test_addTxRingEntry_configured_after_unconfigured_recovers(void)
{
    setNodeCall("XX0XXX-00");
    BuiltFrame f1 = buildPositionFrame(0xAAAAUL);
    TEST_ASSERT_EQUAL_INT(-1, addTxRingEntry(f1.bytes, f1.len, RING_STATUS_READY, "t-still-xx"));

    setNodeCall("DK5EN-91"); // --setcall
    BuiltFrame f2 = buildPositionFrame(0xBBBBUL);
    int slot = addTxRingEntry(f2.bytes, f2.len, RING_STATUS_READY, "t-now-called");

    TEST_ASSERT_EQUAL_INT(0, slot); // Ring war durch den Refuse nie beschrieben worden
    TEST_ASSERT_EQUAL_UINT8_ARRAY(f2.bytes, &ringBuffer[slot][2], f2.len);
}

// ------------------------------------------ Test 3: Refuse-Marker Rate-Limit
//
// "at most one line per 10 s with a dropped-count field" (Auftrag). Die
// Rate-Limit-Zustaende (s_have_marker/s_last_marker_ms/s_refused_since_marker
// in logTxRefuseUnconfigured()) sind function-local static und leben ueber
// die gesamte Laufzeit dieses Testbinaries -- ein "Prime"-Aufruf am
// Testanfang macht das Zeitfenster unabhaengig davon, was vorherige Tests in
// diesem Executable schon ausgeloest haben (siehe Kommentar unten).
static void test_refuse_marker_rate_limited(void)
{
    setNodeCall("XX0XXX-00");
    BuiltFrame f = buildPositionFrame();

    // Prime: ein Aufruf bei ms=0, danach weit genug vorspulen, dass das
    // 10s-Fenster garantiert abgelaufen ist -- unabhaengig vom Zustand, den
    // vorige Tests in diesem Prozess hinterlassen haben.
    mc_test_set_millis(0);
    addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "prime");
    mc_test_set_millis(20000);
    Serial.clear();

    // Erste Ablehnung im frischen Fenster: muss sofort eine Zeile drucken.
    addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "r1");
    std::string afterFirst = Serial.captured();
    TEST_ASSERT_TRUE(afterFirst.size() > 0);
    TEST_ASSERT_NOT_NULL(strstr(afterFirst.c_str(), "[TX];refuse;unconfigured;"));

    // 500ms spaeter, noch im selben Fenster: KEINE weitere Zeile.
    mc_test_set_millis(20500);
    addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "r2");
    TEST_ASSERT_EQUAL_INT((int)afterFirst.size(), (int)Serial.captured().size());

    // >=10s spaeter: Fenster abgelaufen -- neue Zeile, die die unterdrueckte
    // Ablehnung von r2 in "refused;2" mitzaehlt.
    mc_test_set_millis(30501);
    addTxRingEntry(f.bytes, f.len, RING_STATUS_READY, "r3");
    std::string afterThird = Serial.captured();
    TEST_ASSERT_TRUE(afterThird.size() > afterFirst.size());
    TEST_ASSERT_NOT_NULL(strstr(afterThird.c_str(), "refused;2"));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_isUnconfiguredCall_predicate);
    RUN_TEST(test_makeDhcpHostname_configured_call_roundtrip);
    RUN_TEST(test_makeDhcpHostname_lowercase_not_uppercased);
    RUN_TEST(test_makeDhcpHostname_refuses_unconfigured_and_leaves_buffer_untouched);
    RUN_TEST(test_makeDhcpHostname_illegal_chars_mapped_to_hyphen);
    RUN_TEST(test_makeDhcpHostname_trailing_hyphens_trimmed);
    RUN_TEST(test_makeDhcpHostname_all_illegal_chars_returns_false);
    RUN_TEST(test_makeDhcpHostname_truncates_to_buffer_and_never_overruns);
    RUN_TEST(test_makeDhcpHostname_guard_clauses_write_nothing);
    RUN_TEST(test_addTxRingEntry_refuses_when_node_unconfigured);
    RUN_TEST(test_addTxRingEntry_refuses_for_empty_and_none);
    RUN_TEST(test_addTxRingEntry_queues_when_node_configured);
    RUN_TEST(test_addTxRingEntry_configured_after_unconfigured_recovers);
    RUN_TEST(test_refuse_marker_rate_limited);
    return UNITY_END();
}
