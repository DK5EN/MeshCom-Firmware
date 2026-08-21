// Differential-/Snapshot-Runner fuer decodeAPRS() — Mechanismus 1 des
// Test-Orakels aus docs/architecture/08-defect-catalogue.md §4.
//
// Prinzip: test/test_aprs_corpus/corpus.txt haelt on-air mitgeschnittene
// Frames (Hex, ein Frame pro Zeile), golden.txt die eingefrorene kanonische
// Decode-Ausgabe des heutigen Decoders. Jeder kuenftige Decoder-Umbau, der
// das Verhalten gegenueber irgendeinem Korpus-Frame aendert, laesst die
// Suite fehlschlagen — und der git-Diff von golden.txt IST das
// Review-Artefakt ("differential testing" in Snapshot-Form: der Prae-Stand
// steckt in der eingecheckten Datei statt in einem zweiten Binary).
//
// Golden aktualisieren (bewusster Akt, Diff reviewen!):
//   APRS_GOLDEN_UPDATE=1 pio test -e native_aprs -f test_aprs_corpus
//
// Zweite Pruefung pro Frame: Roundtrip-Stabilitaet decode -> encodeAPRS ->
// decode. Verglichen werden nur die Felder, die encodeAPRS erhalten muss
// (Typ, msg_id, Pfad, Ziel, Payload) — FCS und last_hw/mod setzt der
// Encoder naturgemaess neu.
//
//   pio test -e native_aprs

#include <unity.h>

#include <stdio.h>
#include <stdlib.h>
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
uint8_t BOARD_HARDWARE = 9;
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- Helfer

static uint16_t hex2bin(const char *hex, uint8_t *out, uint16_t maxlen)
{
    uint16_t n = 0;
    while (hex[0] && hex[1] && n < maxlen)
    {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return -1;
        };
        int h = nib(hex[0]), l = nib(hex[1]);
        if (h < 0 || l < 0) break;
        out[n++] = (uint8_t)((h << 4) | l);
        hex += 2;
    }
    return n;
}

// Datei relativ zum Projekt finden — pio test startet das Binary je nach
// Plattform mit unterschiedlicher cwd.
static FILE *openRel(const char *rel, const char *mode)
{
    static const char *prefixes[] = { "", "../", "../../", "../../../", "../../../../" };
    char path[512];
    for (const char *p : prefixes)
    {
        snprintf(path, sizeof(path), "%s%s", p, rel);
        FILE *f = fopen(path, mode);
        if (f) return f;
    }
    return nullptr;
}

// Kanonische Decode-Zeile: jede verhaltensrelevante Ausgabe des Decoders.
static void canonical(char *out, size_t outsz, uint16_t rc, const struct aprsMessage &m)
{
    snprintf(out, outsz,
             "rc=%02X id=%08X hop=%u srv=%d mesh=%d track=%d "
             "srcpath=%s dstpath=%s dstcall=%s payload=%s "
             "fcs=%04X hw=%u mod=%u fw=%u lasthw=%u",
             (unsigned)rc, m.msg_id, (unsigned)(m.max_hop & 0x0F),
             (int)m.msg_server, (int)m.msg_mesh, (int)m.msg_track,
             m.msg_source_path.c_str(), m.msg_destination_path.c_str(),
             m.msg_destination_call.c_str(), m.msg_payload.c_str(),
             m.msg_fcs, (unsigned)m.msg_source_hw, (unsigned)m.msg_source_mod,
             (unsigned)m.msg_source_fw_version, (unsigned)m.msg_last_hw);
}

// Reduzierte Zeile fuer den Roundtrip-Vergleich (Encoder erneuert FCS,
// last_hw, mod — die restlichen Felder muessen stabil bleiben).
static void canonicalCore(char *out, size_t outsz, uint16_t rc, const struct aprsMessage &m)
{
    snprintf(out, outsz,
             "rc=%02X id=%08X srcpath=%s dstpath=%s payload=%s",
             (unsigned)rc, m.msg_id,
             m.msg_source_path.c_str(), m.msg_destination_path.c_str(),
             m.msg_payload.c_str());
}

#define MAX_FRAMES   64
#define HEXLINE_MAX  1024
#define CANON_MAX    1024

static char frame_name[MAX_FRAMES][32];
static char frame_hex[MAX_FRAMES][HEXLINE_MAX];
static int  frame_count = 0;

static void loadCorpus(void)
{
    FILE *f = openRel("test/test_aprs_corpus/corpus.txt", "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "corpus.txt nicht gefunden (cwd?)");
    char line[HEXLINE_MAX + 64];
    while (frame_count < MAX_FRAMES && fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char name[32], hex[HEXLINE_MAX];
        if (sscanf(line, "%31s %1023s", name, hex) == 2)
        {
            snprintf(frame_name[frame_count], sizeof(frame_name[0]), "%s", name);
            snprintf(frame_hex[frame_count], sizeof(frame_hex[0]), "%s", hex);
            frame_count++;
        }
    }
    fclose(f);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frame_count, "corpus.txt ist leer");
}

static void test_corpus_gegen_golden(void)
{
    loadCorpus();

    bool update = (getenv("APRS_GOLDEN_UPDATE") != nullptr);

    // Ist-Zeilen erzeugen
    static char actual[MAX_FRAMES][CANON_MAX];
    for (int i = 0; i < frame_count; i++)
    {
        uint8_t buf[UDP_TX_BUF_SIZE] = {0};
        uint16_t len = hex2bin(frame_hex[i], buf, sizeof(buf));
        struct aprsMessage m;
        initAPRS(m, 0x00);
        uint16_t rc = decodeAPRS(buf, len, m);
        canonical(actual[i], CANON_MAX, rc, m);
    }

    if (update)
    {
        FILE *g = openRel("test/test_aprs_corpus/golden.txt", "w");
        TEST_ASSERT_NOT_NULL_MESSAGE(g, "golden.txt nicht schreibbar");
        fprintf(g, "# Eingefrorene kanonische decodeAPRS()-Ausgabe je Korpus-Frame.\n");
        fprintf(g, "# Regenerieren NUR bewusst: APRS_GOLDEN_UPDATE=1 pio test -e native_aprs\n");
        fprintf(g, "# — und den git-Diff dieser Datei reviewen: er IST die Verhaltensaenderung.\n");
        for (int i = 0; i < frame_count; i++)
            fprintf(g, "%s %s\n", frame_name[i], actual[i]);
        fclose(g);
        TEST_MESSAGE("golden.txt neu geschrieben (APRS_GOLDEN_UPDATE)");
    }

    // Golden lesen und vergleichen
    FILE *g = openRel("test/test_aprs_corpus/golden.txt", "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(g, "golden.txt nicht gefunden — einmal mit APRS_GOLDEN_UPDATE=1 erzeugen");
    static char golden[MAX_FRAMES][CANON_MAX];
    int golden_count = 0;
    char line[CANON_MAX + 64];
    while (golden_count < MAX_FRAMES && fgets(line, sizeof(line), g))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char name[32];
        char rest[CANON_MAX];
        rest[0] = 0;
        if (sscanf(line, "%31s %1023[^\n]", name, rest) >= 1)
        {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(frame_name[golden_count], name,
                                             "golden.txt und corpus.txt sind nicht synchron");
            snprintf(golden[golden_count], CANON_MAX, "%s", rest);
            golden_count++;
        }
    }
    fclose(g);
    TEST_ASSERT_EQUAL_INT_MESSAGE(frame_count, golden_count,
                                  "Frame-Anzahl in golden.txt weicht vom Korpus ab");

    for (int i = 0; i < frame_count; i++)
        TEST_ASSERT_EQUAL_STRING_MESSAGE(golden[i], actual[i], frame_name[i]);
}

static void test_roundtrip_decode_encode_decode(void)
{
    if (frame_count == 0) loadCorpus();

    for (int i = 0; i < frame_count; i++)
    {
        uint8_t buf[UDP_TX_BUF_SIZE] = {0};
        uint16_t len = hex2bin(frame_hex[i], buf, sizeof(buf));

        struct aprsMessage m1;
        initAPRS(m1, 0x00);
        uint16_t rc1 = decodeAPRS(buf, len, m1);
        if (rc1 == 0) continue;      // abgelehnte Frames haben keinen Roundtrip
        if (rc1 == 0x41) continue;   // kompakte Binaer-ACKs: decodeAPRS klassifiziert
                                     // nur (keine Felder), encodeAPRS kann sie nicht
                                     // erzeugen -- kein Roundtrip definierbar

        uint8_t reenc[UDP_TX_BUF_SIZE] = {0};
        uint16_t relen = encodeAPRS(reenc, m1);
        TEST_ASSERT_GREATER_THAN_MESSAGE(0, relen, frame_name[i]);

        struct aprsMessage m2;
        initAPRS(m2, 0x00);
        uint16_t rc2 = decodeAPRS(reenc, relen, m2);

        char c1[CANON_MAX], c2[CANON_MAX];
        canonicalCore(c1, sizeof(c1), rc1, m1);
        canonicalCore(c2, sizeof(c2), rc2, m2);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c1, c2, frame_name[i]);
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_corpus_gegen_golden);
    RUN_TEST(test_roundtrip_decode_encode_decode);
    return UNITY_END();
}
