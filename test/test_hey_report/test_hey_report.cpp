// Native Testsuite fuer appendHeySignalReport() — den HEY-Signal-Report
// "NCT,RSSI,SNR;", den ein Node beim Weiterleiten (Mesh) und ein Gateway
// beim UDP-Upload an die '@'-Payload anhaengt.
//
// Hintergrund: bis v4.35p wurde der Report NUR im Relay-Pfad (HF) angehaengt;
// der Gateway-Upload an den Server ging mit der rohen Payload raus. Seit dem
// Fix haengt das Gateway den Report vor addNodeData() an — beide Pfade rufen
// dieselbe Funktion, deren Format hier festgenagelt wird.
//
//   pio test -e native_aprs -f test_hey_report

#include "../../src/mc_text.h"
#include <unity.h>

#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // int statt uint8_t: ODR-Begruendung siehe test_txring.cpp (Verdict Finding 4)
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

// Report an eine frische HEY-Payload: RSSI wird positiv, ';' terminiert
static void test_report_format(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R0;");

    appendHeySignalReport(m, -118, 7, 5);

    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;", m.msg_payload);
}

// Negativer SNR bleibt vorzeichenbehaftet
static void test_report_negativer_snr(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R2;");

    appendHeySignalReport(m, -95, -3, 0);

    TEST_ASSERT_EQUAL_STRING("R2;0,95,-3;", m.msg_payload);
}

// Mehrere Hops haengen ihre Reports hintereinander (Kette bleibt lesbar)
static void test_report_kette(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R0;");

    appendHeySignalReport(m, -118, 7, 5);
    appendHeySignalReport(m, -95, -1, 2);

    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;2,95,-1;", m.msg_payload);
}

// Der angereicherte Report ueberlebt encode -> decode (Wire-Roundtrip),
// d.h. genau diese Payload kommt beim Server bzw. naechsten Hop an.
static void test_report_roundtrip(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_id = 0x11223344;
    mcSet(m.msg_source_path, sizeof(m.msg_source_path), "DK5EN-90");
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "HG");
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "HG");
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R0;");

    appendHeySignalReport(m, -118, 7, 5);

    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = encodeAPRS(buf, m);
    TEST_ASSERT_GREATER_THAN_UINT16(0, len);

    struct aprsMessage d;
    initAPRS(d, 0x00);
    uint16_t t = decodeAPRS(buf, len, d);
    TEST_ASSERT_EQUAL_UINT16('@', t);
    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;", d.msg_payload);
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", d.msg_source_call);
    TEST_ASSERT_EQUAL_STRING("HG", d.msg_destination_call);
}

// ---- Laengenschranke HEY_PATH_PAYLOAD_MAX ---------------------------------
// Die Kette waechst je Relais um bis zu HEY_REPORT_GROUP_MAX Zeichen. Regulaer
// begrenzt MAX_HOP_LIMIT die Zahl der Gruppen, ein von der Luft kommendes
// '@'-Paket mit ueberlanger Nutzlast aber nicht. Ohne Schranke waechst der
// re-encodierte Rahmen ueber UDP_TX_BUF_SIZE und wird dort auf Byteebene
// gekappt -- mitten in einer Gruppe, was updateHeyPath() nicht mehr parsen kann.

// Eine regulaere Kette ueber die volle Hop-Tiefe darf die Schranke NICHT
// beruehren: sonst kuerzt der Fix gueltige Pfade.
static void test_report_volle_hoptiefe_bleibt_unangetastet(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R80;");

    // unguenstigste regulaere Gruppe: "80,128,-128;" (12 Zeichen)
    for(int hop = 0; hop < MAX_HOP_LIMIT; hop++)
        appendHeySignalReport(m, -128, -128, 80);

    // 4 + 7*12 = 88 Zeichen, alle sieben Gruppen sind angehaengt
    TEST_ASSERT_EQUAL_size_t(4u + (size_t)MAX_HOP_LIMIT * 12u, strlen(m.msg_payload));
    TEST_ASSERT_TRUE(strlen(m.msg_payload) <= HEY_PATH_PAYLOAD_MAX);
}

// Direkt unterhalb der Schranke wird noch angehaengt.
static void test_report_schranke_untere_kante(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    m.msg_payload[0] = 0;   // war String('x', 0) -- gemeint war: leer anfangen
    while(strlen(m.msg_payload) < (unsigned)(HEY_PATH_PAYLOAD_MAX - HEY_REPORT_GROUP_MAX))
        mcAppendChar(m.msg_payload, sizeof(m.msg_payload), 'x');

    const unsigned before = strlen(m.msg_payload);
    appendHeySignalReport(m, -95, -3, 0);

    TEST_ASSERT_TRUE_MESSAGE(strlen(m.msg_payload) > before,
                             "genau an der Kante muss noch angehaengt werden");
}

// Ein Zeichen darueber wird nicht mehr angehaengt -- und die vorhandene Kette
// bleibt unveraendert, statt abgeschnitten zu werden.
static void test_report_schranke_kappt_nicht_sondern_beendet(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R80;");
    while(strlen(m.msg_payload) < (unsigned)(HEY_PATH_PAYLOAD_MAX - HEY_REPORT_GROUP_MAX + 1))
        mcAppendChar(m.msg_payload, sizeof(m.msg_payload), 'x');

    const String before = m.msg_payload;
    appendHeySignalReport(m, -95, -3, 0);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), m.msg_payload,
                                     "ueber der Schranke darf nichts angehaengt und nichts gekappt werden");
}

// Auch wiederholte Aufrufe treiben die Nutzlast nicht ueber die Schranke --
// der Fall, den ein fehlerhaft geflutetes '@'-Paket ausloesen wuerde.
static void test_report_bleibt_beschraenkt(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R80;");

    for(int i = 0; i < 50; i++)
        appendHeySignalReport(m, -128, -128, 80);

    TEST_ASSERT_TRUE_MESSAGE(strlen(m.msg_payload) <= HEY_PATH_PAYLOAD_MAX,
                             "Nutzlast muss unter HEY_PATH_PAYLOAD_MAX bleiben");
}

// ---- HEY-UP-01: Empfaenger im Upload-Pfad ---------------------------------
static void mkHey(struct aprsMessage &m, const char *path)
{
    initAPRS(m, '@');
    m.msg_id = 0x11223344;
    mcSet(m.msg_source_path, sizeof(m.msg_source_path), path);
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "HG");
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "HG");
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R0;");
    appendHeySignalReport(m, -118, 7, 5);
}

static void decodeBack(uint8_t *buf, uint16_t len, struct aprsMessage &d)
{
    initAPRS(d, 0x00);
    TEST_ASSERT_EQUAL_UINT16('@', decodeAPRS(buf, len, d));
}

static void test_uploader_direkt(void)
{
    struct aprsMessage m;
    mkHey(m, "DL1ORG-1");
    uint8_t buf[UDP_TX_BUF_SIZE];
    uint16_t len = encodeHeyUpload(buf, m, "DL3OWN-9");
    TEST_ASSERT_GREATER_THAN_UINT16(0, len);

    struct aprsMessage d;
    decodeBack(buf, len, d);
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1,DL3OWN-9", d.msg_source_path);
    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;", d.msg_payload);
}

static void test_uploader_relayed(void)
{
    struct aprsMessage m;
    mkHey(m, "DL1ORG-1,DL2REL-2");
    uint8_t buf[UDP_TX_BUF_SIZE];
    uint16_t len = encodeHeyUpload(buf, m, "DL3OWN-9");

    struct aprsMessage d;
    decodeBack(buf, len, d);
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1,DL2REL-2,DL3OWN-9", d.msg_source_path);
}

static void test_uploader_struct_unveraendert(void)
{
    struct aprsMessage m;
    mkHey(m, "DL1ORG-1,DL2REL-2");
    uint8_t buf[UDP_TX_BUF_SIZE];
    encodeHeyUpload(buf, m, "DL3OWN-9");
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1,DL2REL-2", m.msg_source_path);
    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;", m.msg_payload);
}

static void test_uploader_overflow(void)
{
    struct aprsMessage m;
    mkHey(m, "DL1ORG-1");
    // Pfad auf MC_PATH_LEN-1 Zeichen auffuellen
    char longp[MC_PATH_LEN];
    mcSet(longp, sizeof(longp), "DL1ORG-1");
    while(strlen(longp) + 9 < (size_t)(MC_PATH_LEN - 1))
        mcAppend(longp, sizeof(longp), ",DL4AAA-1");
    mcSet(m.msg_source_path, sizeof(m.msg_source_path), longp);

    TEST_ASSERT_FALSE(appendUploaderToPath(m, "DL3OWN-9-XXXXXXXX"));
    TEST_ASSERT_EQUAL_STRING(longp, m.msg_source_path);

    uint8_t a[UDP_TX_BUF_SIZE], b[UDP_TX_BUF_SIZE] = {0};
    uint16_t la = encodeHeyUpload(a, m, "DL3OWN-9-XXXXXXXX");
    uint16_t lb = encodeAPRS(b, m);
    TEST_ASSERT_EQUAL_UINT16(lb, la);
    TEST_ASSERT_EQUAL_MEMORY(b, a, lb);
}

// Pfad aus Rufzeichen-Elementen mit genau n Zeichen (n >= 9): "DL4AAA-1,..." und
// ein letztes, per 'A' gedehntes Element.
static void mkPathLen(char *out, size_t cap, size_t n)
{
    mcSet(out, cap, "DL4AAA-1");
    while(strlen(out) + 9 + 9 <= n)
        mcAppend(out, cap, ",DL4AAA-1");
    size_t rest = n - strlen(out) - 1;          // Komma + letztes Element
    char last[40] = "DL4";
    size_t k = rest - 3;
    memset(last + 3, 'A', k);
    last[3 + k] = '\0';
    mcAppendChar(out, cap, ',');
    mcAppend(out, cap, last);
}

static void test_uploader_kante_120(void)
{
    // Decodierbarer Quellpfad hat hoechstens MC_PATH_LEN-2 (119) Zeichen
    // (decodeAPRS-Schleife). Ergebnis 119: wird angehaengt UND der Upload-Rahmen
    // decodiert; Ergebnis 120: bleibt unveraendert.
    struct aprsMessage m;
    const char *own = "DL3OWN-9";
    size_t lp = (MC_PATH_LEN - 2) - 1 - strlen(own);
    char longp[MC_PATH_LEN];

    mkPathLen(longp, sizeof(longp), lp);
    mkHey(m, "DL1ORG-1");
    mcSet(m.msg_source_path, sizeof(m.msg_source_path), longp);
    uint8_t buf[UDP_TX_BUF_SIZE];
    uint16_t len = encodeHeyUpload(buf, m, own);
    struct aprsMessage d;
    decodeBack(buf, len, d);
    size_t dl = strlen(d.msg_source_path);
    TEST_ASSERT_EQUAL_size_t(MC_PATH_LEN - 2, dl);
    TEST_ASSERT_EQUAL_STRING(own, d.msg_source_path + dl - strlen(own));

    TEST_ASSERT_TRUE(appendUploaderToPath(m, own));
    TEST_ASSERT_EQUAL_size_t(MC_PATH_LEN - 2, strlen(m.msg_source_path));

    mkPathLen(longp, sizeof(longp), lp + 1);
    mcSet(m.msg_source_path, sizeof(m.msg_source_path), longp);
    TEST_ASSERT_FALSE(appendUploaderToPath(m, own));
    TEST_ASSERT_EQUAL_STRING(longp, m.msg_source_path);
}

static void test_appendcall_erfolg(void)
{
    char p[MC_PATH_LEN];
    mcSet(p, sizeof(p), "DL1ORG-1");
    TEST_ASSERT_TRUE(appendCallToPath(p, sizeof(p), "DL3OWN-9"));
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1,DL3OWN-9", p);
}

static void test_appendcall_verweigert_ohne_komma(void)
{
    char p[MC_PATH_LEN], ref[MC_PATH_LEN];
    const char *own = "DL3OWN-9";
    // Ergebnis genau 119: ok; Ergebnis 120: abgelehnt, Pfad byte-identisch
    mkPathLen(p, sizeof(p), MC_PATH_MAX_CHARS - 1 - strlen(own));
    TEST_ASSERT_TRUE(appendCallToPath(p, sizeof(p), own));
    TEST_ASSERT_EQUAL_size_t(MC_PATH_MAX_CHARS, strlen(p));

    mkPathLen(p, sizeof(p), MC_PATH_MAX_CHARS - strlen(own));
    memcpy(ref, p, sizeof(ref));
    TEST_ASSERT_FALSE(appendCallToPath(p, sizeof(p), own));
    TEST_ASSERT_EQUAL_MEMORY(ref, p, sizeof(ref));
    TEST_ASSERT_EQUAL_size_t(MC_PATH_MAX_CHARS - strlen(own), strlen(p));
}

static void test_appendcall_leer(void)
{
    char p[MC_PATH_LEN];
    mcSet(p, sizeof(p), "DL1ORG-1");
    TEST_ASSERT_FALSE(appendCallToPath(p, sizeof(p), ""));
    TEST_ASSERT_FALSE(appendCallToPath(p, sizeof(p), NULL));
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1", p);
}

static void test_report_rueckgabewert(void)
{
    struct aprsMessage m;
    initAPRS(m, '@');
    mcSet(m.msg_payload, sizeof(m.msg_payload), "R0;");
    TEST_ASSERT_TRUE(appendHeySignalReport(m, -118, 7, 5));
    TEST_ASSERT_EQUAL_STRING("R0;5,118,7;", m.msg_payload);

    // Nutzlast so lang, dass keine Gruppe mehr passt
    size_t n = HEY_PATH_PAYLOAD_MAX - HEY_REPORT_GROUP_MAX + 1;
    memset(m.msg_payload, 'x', n);
    m.msg_payload[n] = '\0';
    TEST_ASSERT_FALSE(appendHeySignalReport(m, -118, 7, 5));
    TEST_ASSERT_EQUAL_size_t(n, strlen(m.msg_payload));
}

static void test_uploader_schon_im_pfad(void)
{
    struct aprsMessage m;
    mkHey(m, "DL1ORG-1,DL3OWN-9,DL2REL-2");
    TEST_ASSERT_FALSE(appendUploaderToPath(m, "DL3OWN-9"));
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1,DL3OWN-9,DL2REL-2", m.msg_source_path);

    mkHey(m, "DL1ORG-1,DL3OWN-9");
    TEST_ASSERT_FALSE(appendUploaderToPath(m, "DL3OWN-9"));
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1,DL3OWN-9", m.msg_source_path);
}

static void test_uploader_teiltreffer(void)
{
    struct aprsMessage m;
    mkHey(m, "DL3OWN-91");
    TEST_ASSERT_TRUE(appendUploaderToPath(m, "DL3OWN-9"));
    TEST_ASSERT_EQUAL_STRING("DL3OWN-91,DL3OWN-9", m.msg_source_path);
}

static void test_uploader_leer(void)
{
    struct aprsMessage m;
    mkHey(m, "DL1ORG-1");
    TEST_ASSERT_FALSE(appendUploaderToPath(m, ""));
    TEST_ASSERT_FALSE(appendUploaderToPath(m, NULL));
    TEST_ASSERT_EQUAL_STRING("DL1ORG-1", m.msg_source_path);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_report_format);
    RUN_TEST(test_report_negativer_snr);
    RUN_TEST(test_report_kette);
    RUN_TEST(test_report_roundtrip);
    RUN_TEST(test_report_volle_hoptiefe_bleibt_unangetastet);
    RUN_TEST(test_report_schranke_untere_kante);
    RUN_TEST(test_report_schranke_kappt_nicht_sondern_beendet);
    RUN_TEST(test_report_bleibt_beschraenkt);
    RUN_TEST(test_uploader_direkt);
    RUN_TEST(test_uploader_relayed);
    RUN_TEST(test_uploader_struct_unveraendert);
    RUN_TEST(test_uploader_overflow);
    RUN_TEST(test_uploader_kante_120);
    RUN_TEST(test_appendcall_erfolg);
    RUN_TEST(test_appendcall_verweigert_ohne_komma);
    RUN_TEST(test_appendcall_leer);
    RUN_TEST(test_report_rueckgabewert);
    RUN_TEST(test_uploader_schon_im_pfad);
    RUN_TEST(test_uploader_teiltreffer);
    RUN_TEST(test_uploader_leer);
    return UNITY_END();
}
