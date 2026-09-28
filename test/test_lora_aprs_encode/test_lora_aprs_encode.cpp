// Track-Beacon (encodeLoRaAPRScompressed) auf der LoRa-APRS-Trackfrequenz.
//
// Issue icssw-org/MeshCom-Firmware#1174: der komprimierte Positionsframe ging
// seit V4.27/c ohne Digipeater-Pfad raus ("CALL>APRSMC:!..."), waehrend die
// beiden anderen LoRa-APRS-Encoder (unkomprimierte Position, Text/Telemetrie)
// "WIDE1-1" setzen. Standard-LoRa-APRS-Digipeater wiederholen einen Frame ohne
// WIDE1-1 nicht.
//
// Der Golden-Frame ist die Ausgabe des Encoders VOR dem Fix, nur mit
// ",WIDE1-1" hinter dem Tocall -- er belegt also zugleich, dass sich an der
// komprimierten Position und am Kommentar nichts geaendert hat.
//
//   pio test -e native_aprs_fuzz -f test_lora_aprs_encode

#include <unity.h>

#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

#define GOLDEN_TRACK_FRAME "XX0XXX-1>APRSMC,WIDE1-1:!/61EEQENN> P[track"

static char cCall[10] = "XX0XXX-1";

void setUp(void)
{
    memset(&meshcom_settings, 0, sizeof(meshcom_settings));
    strcpy(meshcom_settings.node_aprsmc, "APRSMC");
    strcpy(meshcom_settings.node_atxt, "track");
    meshcom_settings.node_symid = '/';
    meshcom_settings.node_symcd = '>';
}

void tearDown(void) {}

static uint16_t encodeTrack(uint8_t buf[UDP_TX_BUF_SIZE])
{
    memset(buf, 0, UDP_TX_BUF_SIZE);
    return encodeLoRaAPRScompressed(buf, cCall, 48.1, 'N', 11.5, 'E', 0);
}

// Der TNC2-Text hinter dem Drei-Byte-Praefix.
static const char *tnc2(uint8_t buf[UDP_TX_BUF_SIZE])
{
    return (const char *)buf + 3;
}

void test_track_beacon_keeps_lora_aprs_prefix(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    uint16_t ilng = encodeTrack(buf);

    TEST_ASSERT_EQUAL_UINT8('<', buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[2]);
    TEST_ASSERT_EQUAL_UINT16(strlen(tnc2(buf)) + 3, ilng);
}

void test_track_beacon_carries_wide1_1_path(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    encodeTrack(buf);

    TEST_ASSERT_EQUAL_STRING_LEN("XX0XXX-1>APRSMC,WIDE1-1:!", tnc2(buf), 25);
}

void test_track_beacon_path_follows_configured_tocall(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    strcpy(meshcom_settings.node_aprsmc, "APLT00");
    encodeTrack(buf);

    TEST_ASSERT_EQUAL_STRING_LEN("XX0XXX-1>APLT00,WIDE1-1:!", tnc2(buf), 25);
}

void test_track_beacon_golden_frame(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    encodeTrack(buf);

    TEST_ASSERT_EQUAL_STRING(GOLDEN_TRACK_FRAME, tnc2(buf));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_track_beacon_keeps_lora_aprs_prefix);
    RUN_TEST(test_track_beacon_carries_wide1_1_path);
    RUN_TEST(test_track_beacon_path_follows_configured_tocall);
    RUN_TEST(test_track_beacon_golden_frame);
    return UNITY_END();
}
