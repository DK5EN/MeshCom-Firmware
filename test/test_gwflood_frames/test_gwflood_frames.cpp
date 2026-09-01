// TM-31 / upstream #568: die Frames, die der Injektor auf ein Gateway wirft,
// muessen Frames sein, die die Firmware auch annimmt.
//
// gwflood.py baut sie in Python -- msg_id ersetzt, Nutzlast neu gesetzt, FCS
// nachgerechnet. Genau da war die erste Fassung blind: ein Frame mit falscher
// Pruefsumme oder verrutschtem Aufbau wird vom Gateway still verworfen, und die
// Messung meldet "0 gesendet", ohne dass irgendetwas am Gateway kaputt waere.
// Diese Suite faehrt deshalb den ECHTEN decodeAPRS() ueber die eingecheckte
// Fixture und prueft Typ, msg_id, Pfad, Ziel, Nutzlast und die Wire-FCS.
//
// Fixture erneuern (bewusster Akt, Diff reviewen):
//   python3 tools/bench/experiments/gwflood.py --frame mixed \
//       --dump-frames test/support/gwflood_frames.txt
//
//   pio test -e native_aprs -f test_gwflood_frames

#include <unity.h>

#include <stdio.h>
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
int BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

// ---- Fixture laden ---------------------------------------------------------

#define MAX_FIXTURE_FRAMES 40

struct Fixture
{
    char name[24];
    uint8_t bytes[UDP_TX_BUF_SIZE];
    uint16_t len;
};

static Fixture g_frames[MAX_FIXTURE_FRAMES];
static int g_count = 0;

static int hexToBytes(const char *hex, uint8_t *out, int max)
{
    int n = 0;
    while (hex[0] && hex[1] && n < max)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = nib(hex[0]), l = nib(hex[1]);
        if (h < 0 || l < 0) break;
        out[n++] = (uint8_t)((h << 4) | l);
        hex += 2;
    }
    return n;
}

// pio test startet das Binary je nach Plattform mit unterschiedlicher cwd.
static FILE *openRel(const char *rel)
{
    static const char *prefixes[] = { "", "../", "../../", "../../../", "../../../../" };
    char path[512];
    for (const char *p : prefixes)
    {
        snprintf(path, sizeof(path), "%s%s", p, rel);
        FILE *f = fopen(path, "r");
        if (f) return f;
    }
    return nullptr;
}

static void loadFixture(void)
{
    if (g_count > 0) return;

    FILE *f = openRel("test/support/gwflood_frames.txt");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "gwflood_frames.txt nicht gefunden (cwd?)");

    char line[1024];
    while (fgets(line, sizeof(line), f) != nullptr)
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char name[24] = {0};
        char hex[900] = {0};
        if (sscanf(line, "%23s %899s", name, hex) != 2) continue;
        TEST_ASSERT_LESS_THAN_MESSAGE(MAX_FIXTURE_FRAMES, g_count,
                                      "mehr Frames als MAX_FIXTURE_FRAMES -- Limit erhoehen");
        Fixture &fx = g_frames[g_count];
        snprintf(fx.name, sizeof(fx.name), "%s", name);
        fx.len = (uint16_t)hexToBytes(hex, fx.bytes, (int)sizeof(fx.bytes));
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, fx.len, "Frame ohne Bytes");
        g_count++;
    }
    fclose(f);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, g_count, "gwflood_frames.txt ist leer");
}

// Wire-FCS: die Bytesumme vor der Pruefsumme, 2 Byte gross an len-6, danach
// 4 Byte Trailer -- dieselbe Rechnung wie in gwflood.py und wie sie fuer alle
// 12 pruefsummentragenden Korpus-Frames gilt.
static uint16_t wireFcsCalc(const Fixture &fx)
{
    uint32_t sum = 0;
    int at = (int)fx.len - 6;
    for (int i = 0; i < at; i++) sum += fx.bytes[i];
    return (uint16_t)(sum & 0xFFFF);
}

static uint16_t wireFcsStored(const Fixture &fx)
{
    int at = (int)fx.len - 6;
    return (uint16_t)((fx.bytes[at] << 8) | fx.bytes[at + 1]);
}

// ---- Tests -----------------------------------------------------------------

static void test_fixture_enthaelt_beide_frame_arten(void)
{
    loadFixture();

    int pos = 0, text = 0;
    for (int i = 0; i < g_count; i++)
    {
        if (g_frames[i].bytes[0] == 0x21) pos++;
        if (g_frames[i].bytes[0] == 0x3A) text++;
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, pos, "keine Positionsframes in der Fixture");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, text, "keine Textframes in der Fixture -- "
                                              "#568 ist genau der Textfall");
}

static void test_wire_fcs_stimmt(void)
{
    loadFixture();
    for (int i = 0; i < g_count; i++)
    {
        char msg[64];
        snprintf(msg, sizeof(msg), "%s: FCS falsch gerechnet", g_frames[i].name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(wireFcsCalc(g_frames[i]),
                                         wireFcsStored(g_frames[i]), msg);
    }
}

static void test_decoder_nimmt_jeden_frame_an(void)
{
    loadFixture();
    for (int i = 0; i < g_count; i++)
    {
        Fixture &fx = g_frames[i];
        struct aprsMessage m;
        initAPRS(m, 0);
        uint16_t rc = decodeAPRS(fx.bytes, fx.len, m);

        char msg[80];
        snprintf(msg, sizeof(msg), "%s: decodeAPRS() hat den Frame abgewiesen", fx.name);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, rc, msg);

        snprintf(msg, sizeof(msg), "%s: payload_type falsch decodiert", fx.name);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(fx.bytes[0], (uint8_t)m.payload_type, msg);

        // msg_id steht little-endian in [1:5] -- das ist die Kennung, ueber die
        // gwflood Ingress, Queue, TX und Observer-RX zusammenfuehrt. Stimmt sie
        // nicht, zaehlt die Messung ins Leere.
        unsigned int want_id = (unsigned int)fx.bytes[1] |
                               ((unsigned int)fx.bytes[2] << 8) |
                               ((unsigned int)fx.bytes[3] << 16) |
                               ((unsigned int)fx.bytes[4] << 24);
        snprintf(msg, sizeof(msg), "%s: msg_id falsch decodiert", fx.name);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(want_id, m.msg_id, msg);

        snprintf(msg, sizeof(msg), "%s: Quellpfad leer", fx.name);
        TEST_ASSERT_TRUE_MESSAGE(m.msg_source_path.length() > 0, msg);

        snprintf(msg, sizeof(msg), "%s: FCS-Feld nicht uebernommen", fx.name);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(wireFcsStored(fx), (uint16_t)m.msg_fcs, msg);
    }
}

static void test_textframes_sind_broadcast_und_128_byte_klasse(void)
{
    loadFixture();

    int checked = 0;
    for (int i = 0; i < g_count; i++)
    {
        Fixture &fx = g_frames[i];
        if (fx.bytes[0] != 0x3A) continue;
        if (strncmp(fx.name, "gtext0", 6) != 0) continue;   // die Default-Laenge

        struct aprsMessage m;
        initAPRS(m, 0);
        TEST_ASSERT_NOT_EQUAL(0, decodeAPRS(fx.bytes, fx.len, m));

        char msg[80];
        // Broadcast "*" -> getMessagePriority() == MSG_PRIO_HIGH, die Klasse,
        // um die es in #568 geht (siehe test_txring_flood).
        snprintf(msg, sizeof(msg), "%s: Ziel ist nicht der Broadcast '*'", fx.name);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("*", m.msg_destination_call.c_str(), msg);

        snprintf(msg, sizeof(msg), "%s: Nutzlast leer", fx.name);
        TEST_ASSERT_TRUE_MESSAGE(m.msg_payload.length() > 0, msg);

        // #568: alle sieben Meldungen waren ~128 Byte gross.
        snprintf(msg, sizeof(msg), "%s: Frame ist %u Byte, erwartet 128", fx.name, fx.len);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(128, fx.len, msg);
        checked++;
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, checked, "keine Default-Textframes geprueft");
}

static void test_laengenvarianten_dekodieren_ebenfalls(void)
{
    loadFixture();

    int checked = 0;
    for (int i = 0; i < g_count; i++)
    {
        Fixture &fx = g_frames[i];
        if (strncmp(fx.name, "gtextlen", 8) != 0) continue;

        struct aprsMessage m;
        initAPRS(m, 0);
        char msg[80];
        snprintf(msg, sizeof(msg), "%s: Laengenvariante abgewiesen", fx.name);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(0, decodeAPRS(fx.bytes, fx.len, m), msg);
        TEST_ASSERT_TRUE_MESSAGE(m.msg_payload.length() > 0, msg);
        checked++;
    }
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, checked, "keine Laengenvarianten in der Fixture");
}

static void test_jede_msg_id_kommt_nur_einmal_vor(void)
{
    loadFixture();

    // Der Injektor fuehrt Ingress/TX/RX ueber die msg_id zusammen; doppelte
    // Kennungen wuerden am Gateway als Duplikat verworfen und die Messung
    // stillschweigend verfaelschen.
    for (int i = 0; i < g_count; i++)
    {
        for (int j = i + 1; j < g_count; j++)
        {
            bool same = memcmp(&g_frames[i].bytes[1], &g_frames[j].bytes[1], 4) == 0;
            char msg[80];
            snprintf(msg, sizeof(msg), "%s und %s teilen sich eine msg_id",
                     g_frames[i].name, g_frames[j].name);
            TEST_ASSERT_FALSE_MESSAGE(same, msg);
        }
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_fixture_enthaelt_beide_frame_arten);
    RUN_TEST(test_wire_fcs_stimmt);
    RUN_TEST(test_decoder_nimmt_jeden_frame_an);
    RUN_TEST(test_textframes_sind_broadcast_und_128_byte_klasse);
    RUN_TEST(test_laengenvarianten_dekodieren_ebenfalls);
    RUN_TEST(test_jede_msg_id_kommt_nur_einmal_vor);
    return UNITY_END();
}
