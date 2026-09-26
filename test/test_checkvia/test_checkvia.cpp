// Native Testsuite fuer checkVia() -- PT-01 (BACKLOG SS3.8j): der Via-Pfad
// hatte laut BACKLOG-Tabelle "Wave 0.6" schon auf dem Zettel gestanden, aber
// nie eine eigene Suite.
//
// checkVia() (src/via_functions.cpp) ist deutlich schlanker als es aussieht:
// von den drei Zweigen unter "if(strlen(node_via) == 0)" ist nur der
// bGATEWAY-Zweig ueberhaupt vorhanden, und der ist komplett auskommentiert
// ("22.07.2026 - zum Test entfernt"), ebenso der Nicht-Gateway-Zweig (bis
// MeshCom 5 Welle 4 auf MHeard, seither auf die Topologie umgeschrieben,
// weiter auskommentiert). Effektiv bleibt:
//
//   bVIA==false                      -> msg_destination_path unveraendert
//   bVIA==true, node_via leer        -> msg_destination_path unveraendert (toter Code)
//   bVIA==true, node_via gesetzt     -> msg_destination_path = "<node_via>,<msg_destination_call>"
//
// Diese Suite haelt genau das fest, damit ein kuenftiges Wiederaktivieren
// der auskommentierten Zweige eine bewusste Verhaltensaenderung ist und
// keine stille.
//
//   pio test -e native_parsers -f test_checkvia

#include "../../src/mc_text.h"
#include <unity.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <aprs_structures.h>
#include <nrf52/WisBlock-API.h>
#include <parser_link_stubs.h>
#include <via_functions.h>

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp/via_functions.cpp
// (env:native_parsers linkt beide Parser in jedes Testprogramm der Env,
// siehe test/test_decodemheard/stubs/parser_link_stubs.h)
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // RAK4631 -- int statt uint8_t: ODR-Begruendung siehe test_txring.cpp (Verdict Finding 4)
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

// Kleiner Test-lokaler Helfer statt loop_functions.h::count_char() zu
// verlinken (das lebt in loop_functions.cpp, das env:native_parsers bewusst
// nicht mitschleppt) -- eigener Name, um keine Deklaration aus einem
// transitiv eingebundenen Header versehentlich zu kollidieren.
static int countCommas(const String &s)
{
    int n = 0;
    for (unsigned int i = 0; i < s.length(); i++)
        if (s.charAt(i) == ',')
            n++;
    return n;
}

void setUp(void)
{
    // checkVia() liest bVIA/node_via/bGATEWAY als globalen Zustand -- jeder
    // Testfall startet auf demselben bekannten Ausgangspunkt statt vom
    // vorherigen Testfall zu erben.
    bVIA = false;
    bGATEWAY = false;
    meshcom_settings.node_via[0] = 0x00;
}
void tearDown(void) {}

// ------------------------------------------------------------ Testfaelle

// bVIA==false: checkVia() ist ein vollstaendiges No-Op, unabhaengig von
// node_via oder der bisherigen msg_destination_path.
static void test_bvia_aus_bleibt_unveraendert(void)
{
    bVIA = false;
    strncpy(meshcom_settings.node_via, "DB0ABC-1", sizeof(meshcom_settings.node_via) - 1);

    struct aprsMessage m;
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "*");
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "*");

    checkVia(m);

    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_path);
}

// bVIA==true, node_via leer: der einzige noch aktive Zweig braucht
// strlen(node_via)>0 -- ist node_via leer, bleiben BEIDE verbleibenden
// Unterzweige (bGATEWAY true/false) toter, auskommentierter Code. Ergebnis:
// genauso ein No-Op wie bVIA==false.
static void test_bvia_an_node_via_leer_bleibt_unveraendert(void)
{
    bVIA = true;
    bGATEWAY = false;
    // node_via bleibt leer (setUp())

    struct aprsMessage m;
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "*");
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "*");

    checkVia(m);

    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_path);
}

// Derselbe Fall, aber bGATEWAY==true -- macht keinen Unterschied: der
// bGATEWAY-Zweig selbst ist auskommentiert (siehe Datei-Kommentar oben).
static void test_bvia_an_node_via_leer_bgateway_macht_keinen_unterschied(void)
{
    bVIA = true;
    bGATEWAY = true;

    struct aprsMessage m;
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "*");
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "*");

    checkVia(m);

    TEST_ASSERT_EQUAL_STRING("*", m.msg_destination_path);
}

// bVIA==true, node_via gesetzt, Zielrufzeichen OHNE SSID: Pfad wird komplett
// ERSETZT (nicht angehaengt) durch "<node_via>,<dest_call>".
static void test_node_via_gesetzt_ziel_ohne_ssid(void)
{
    bVIA = true;
    strncpy(meshcom_settings.node_via, "DB0ABC-1", sizeof(meshcom_settings.node_via) - 1);

    struct aprsMessage m;
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "*");   // muss komplett ueberschrieben werden
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "*");

    checkVia(m);

    TEST_ASSERT_EQUAL_STRING("DB0ABC-1,*", m.msg_destination_path);
}

// bVIA==true, node_via gesetzt, Zielrufzeichen MIT SSID.
static void test_node_via_gesetzt_ziel_mit_ssid(void)
{
    bVIA = true;
    strncpy(meshcom_settings.node_via, "DB0ABC-1", sizeof(meshcom_settings.node_via) - 1);

    struct aprsMessage m;
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "irrelevant-vorher");
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "OE1KBC-7");

    checkVia(m);

    TEST_ASSERT_EQUAL_STRING("DB0ABC-1,OE1KBC-7", m.msg_destination_path);
}

// Pfadkonstruktion: genau EIN Komma, keine Leerzeichen, node_via zuerst --
// haelt das exakte Trennzeichen-/Reihenfolgeformat fest, auf das
// nachgelagerte Parser (z.B. checkMesh()s pathNamesCall() im selben
// Pfadstring) sich verlassen.
static void test_pfadkonstruktion_genau_ein_komma(void)
{
    bVIA = true;
    strncpy(meshcom_settings.node_via, "OE1XAR-1", sizeof(meshcom_settings.node_via) - 1);

    struct aprsMessage m;
    m.msg_destination_path[0] = 0;
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), "DL2JA-2");

    checkVia(m);

    TEST_ASSERT_EQUAL_STRING("OE1XAR-1,DL2JA-2", m.msg_destination_path);
    TEST_ASSERT_EQUAL_INT(1, countCommas(m.msg_destination_path));
}

// Zielrufzeichen leer (kein SSID, kein Call ueberhaupt -- z.B. ein noch
// unadressiertes Paket): checkVia() validiert das nicht, haengt einfach
// "," + "" an. Kein Crash, aber ein trailing Komma im Ergebnis -- hier nur
// dokumentiert, kein Fixvorschlag.
static void test_leeres_zielrufzeichen_erzeugt_trailing_komma(void)
{
    bVIA = true;
    strncpy(meshcom_settings.node_via, "DB0ABC-1", sizeof(meshcom_settings.node_via) - 1);

    struct aprsMessage m;
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), "*");
    m.msg_destination_call[0] = 0;

    checkVia(m);

    TEST_ASSERT_EQUAL_STRING("DB0ABC-1,", m.msg_destination_path);
}

// ------------------------------------------------------- checkMesh(): --mesh
//
// checkMesh() ist die EINZIGE Relay-Schranke (lora_functions.cpp), und der
// Vertrag im Kopf von via_functions.cpp sagt "false if bMESH == false", ohne
// Ausnahme. Bis zum Fix gab der Via-Zweig hart true zurueck und las bMESH
// nie -- "--mesh off" schaltete das Relay auf Via-Pfaden also nicht ab.

// Hilfsaufbau: Frame mit Via-Pfad, der uns als Hop nennt.
static void mkViaFrame(struct aprsMessage &m, const char *pfad, const char *ziel)
{
    initAPRS(m, ':');
    mcSet(m.msg_source_call, sizeof(m.msg_source_call), "DL2JA-2");   // fremder Absender, nicht wir
    mcSet(m.msg_payload, sizeof(m.msg_payload), "hallo");
    mcSet(m.msg_destination_path, sizeof(m.msg_destination_path), pfad);
    mcSet(m.msg_destination_call, sizeof(m.msg_destination_call), ziel);
}

static void test_mesh_aus_kein_relay_trotz_eigenem_call_im_pfad(void)
{
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", "DK5EN-1");
    struct aprsMessage m;
    mkViaFrame(m, "DK5EN-1,OE1KBC-7", "OE1KBC-7");

    bMESH = false;
    TEST_ASSERT_FALSE(checkMesh(m));
    bMESH = true;
}

static void test_mesh_an_relay_bei_eigenem_call_im_pfad(void)
{
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", "DK5EN-1");
    struct aprsMessage m;
    mkViaFrame(m, "DK5EN-1,OE1KBC-7", "OE1KBC-7");

    bMESH = true;
    TEST_ASSERT_TRUE(checkMesh(m));
}

// Praefix-Kollision: mcIndexOfStr() traf auf jedes Rufzeichen, dessen Praefix
// das eigene ist. DK5EN-1 hielt sich fuer den benannten Hop, sobald DK5EN-14
// im Pfad stand -- beide Rufzeichen gibt es in dieser Flotte.
static void test_fremder_hop_mit_eigenem_call_als_praefix(void)
{
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", "DK5EN-1");
    struct aprsMessage m;
    mkViaFrame(m, "DK5EN-14,OE1KBC-7", "OE1KBC-7");

    bMESH = true;
    TEST_ASSERT_FALSE(checkMesh(m));
}

// Basisrufzeichen darf nicht auf die eigene SSID-Variante matchen.
static void test_basisrufzeichen_matcht_nicht_auf_ssid(void)
{
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", "DK5EN-9");
    struct aprsMessage m;
    mkViaFrame(m, "DK5EN-98,DL2JA-2", "DL2JA-2");

    bMESH = true;
    TEST_ASSERT_FALSE(checkMesh(m));
}

// Ohne Via-Info (Pfad == Zielrufzeichen) gilt bMESH unveraendert -- dieser
// Zweig war nie kaputt und bleibt es.
static void test_ohne_via_info_folgt_bmesh(void)
{
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", "DK5EN-1");
    struct aprsMessage m;
    mkViaFrame(m, "*", "*");

    bMESH = false;
    TEST_ASSERT_FALSE(checkMesh(m));
    bMESH = true;
    TEST_ASSERT_TRUE(checkMesh(m));
}

// Abschliessendes Komma erzeugt checkVia() selbst (s. Testfall oben) -- der
// Token-Vergleich darf daran nicht haengenbleiben.
static void test_trailing_komma_im_pfad(void)
{
    snprintf(meshcom_settings.node_call, sizeof(meshcom_settings.node_call), "%s", "DK5EN-1");
    struct aprsMessage m;
    mkViaFrame(m, "DK5EN-1,", "");

    bMESH = true;
    TEST_ASSERT_TRUE(checkMesh(m));
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_bvia_aus_bleibt_unveraendert);
    RUN_TEST(test_bvia_an_node_via_leer_bleibt_unveraendert);
    RUN_TEST(test_bvia_an_node_via_leer_bgateway_macht_keinen_unterschied);
    RUN_TEST(test_node_via_gesetzt_ziel_ohne_ssid);
    RUN_TEST(test_node_via_gesetzt_ziel_mit_ssid);
    RUN_TEST(test_pfadkonstruktion_genau_ein_komma);
    RUN_TEST(test_leeres_zielrufzeichen_erzeugt_trailing_komma);
    RUN_TEST(test_mesh_aus_kein_relay_trotz_eigenem_call_im_pfad);
    RUN_TEST(test_mesh_an_relay_bei_eigenem_call_im_pfad);
    RUN_TEST(test_fremder_hop_mit_eigenem_call_als_praefix);
    RUN_TEST(test_basisrufzeichen_matcht_nicht_auf_ssid);
    RUN_TEST(test_ohne_via_info_folgt_bmesh);
    RUN_TEST(test_trailing_komma_im_pfad);
    return UNITY_END();
}
