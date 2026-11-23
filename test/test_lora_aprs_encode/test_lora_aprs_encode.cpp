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

// APRS-TXT-01 (#1293): the Track frame carries the same 25 comment bytes as the mesh position.
void test_track_beacon_keeps_25_bytes_of_comment(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    strcpy(meshcom_settings.node_atxt, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123");
    encodeTrack(buf);

    const char *p = strstr(tnc2(buf), " P[");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJKLMNOPQRSTUVWXY", p + 3);
}

void test_track_beacon_cut_never_splits_utf8(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    for (int i = 0; i < 13; i++)
    {
        meshcom_settings.node_atxt[2 * i] = (char)0xC3;
        meshcom_settings.node_atxt[2 * i + 1] = (char)0x84;
    }
    meshcom_settings.node_atxt[26] = 0;
    encodeTrack(buf);

    const char *p = strstr(tnc2(buf), " P[");
    TEST_ASSERT_NOT_NULL(p);
    p += 3;
    TEST_ASSERT_EQUAL_UINT(24, strlen(p));
    for (int i = 0; i < 12; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(0xC3, (unsigned char)p[2 * i]);
        TEST_ASSERT_EQUAL_UINT8(0x84, (unsigned char)p[2 * i + 1]);
    }
}

// R4: worst-case Track frame (39 stored bytes, 25 on air) stays far below the 245-byte clamp.
void test_track_beacon_worst_case_length_is_bounded(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    memset(meshcom_settings.node_atxt, 'A', 39);
    meshcom_settings.node_atxt[39] = 0;
    uint16_t ilng = encodeTrack(buf);

    TEST_ASSERT_EQUAL_UINT(strlen(GOLDEN_TRACK_FRAME) - 5 + 25 + 3, ilng);
    TEST_ASSERT_TRUE(ilng < 245);
}

// F3-8: encodeStartAPRS() clamped its copy to 254 bytes but writes it at msg_buffer+6, so path fields wider than
// today's MC_PATH_LEN (121) wrote 5 bytes past the 255-byte target. The live width cannot reach it, so the test
// makes both strlen() results huge by laying the message into an oversized arena filled with non-NUL bytes.
static void test_encode_start_never_writes_past_target_for_any_path_width()
{
    const size_t arena_len = sizeof(struct aprsMessage) + 600;
    char *arena = new char[arena_len];
    memset(arena, 'A', arena_len);
    arena[arena_len - 1] = '\0';
    struct aprsMessage *m = reinterpret_cast<struct aprsMessage *>(arena);
    m->payload_type = ':';
    m->msg_id = 0x11223344u;
    m->max_hop = 5;
    m->msg_server = false;
    m->msg_track = false;
    m->msg_app_offline = false;

    uint8_t *buf = new uint8_t[UDP_TX_BUF_SIZE];   // exactly the declared target; ASan flags any byte past it
    uint16_t r = encodeStartAPRS(buf, *m);
    TEST_ASSERT_TRUE(r <= UDP_TX_BUF_SIZE);
    delete[] buf;
    delete[] arena;
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
    RUN_TEST(test_track_beacon_keeps_25_bytes_of_comment);
    RUN_TEST(test_track_beacon_cut_never_splits_utf8);
    RUN_TEST(test_track_beacon_worst_case_length_is_bounded);
    RUN_TEST(test_encode_start_never_writes_past_target_for_any_path_width);
    return UNITY_END();
}
