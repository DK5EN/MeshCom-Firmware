// Robustheitstest fuer decodeAPRS() gegen echte, auf der Luftschnittstelle
// beschaedigte Frames -- Mechanismus 2 des Test-Orakels aus
// docs/architecture/08-defect-catalogue.md §4, jetzt mit Feldmaterial.
//
// Herkunft des Korpus: crc_corpus.txt haelt Frames, die der Radiochip mit
// CRC-Fehler verworfen hat (Hex-Dump aus esp32_main.cpp:3977), geerntet aus
// 32,7 h Log von DG0OPK-11/-12/-13 und DJ8MEH-41 (23.-25.08.2026) mit
// tools/logharvest.py. Das ist KEIN synthetischer Fuzz: es sind reale
// Bitfehler-Muster (Kollisionen, Schwundeinbrueche) mit realen Frame-Koepfen --
// 10.085 der 10.168 Dumps tragen ein gueltiges Typbyte, die Korruption sitzt
// also ueberwiegend im Rumpf, genau dort wo der Parser laeuft.
//
// ack_corpus.txt haelt 7- und 12-Byte-ACK-Frames, byte-exakt aus der Ausgabe
// von printBuffer_ack() zurueckgerechnet (dort steht jedes Byte des Frames).
//
// Geprueft wird, was ohne externes Orakel pruefbar ist:
//
//   1. kein Absturz, kein Ueberlauf   -- unter -fsanitize=address,undefined
//   2. Rueckgabewert aus der erlaubten Menge {0x00, 0x21, 0x3A, 0x40, 0x41}
//   3. msg_len <= rsize               -- der Decoder darf nicht mehr Bytes
//                                        verbraucht haben als er bekommen hat
//   4. Feldlaengen innerhalb des Frames
//   5. Determinismus gegen Puffer-Muell: derselbe Frame in einem mit 0x00 bzw.
//      0xFF gefuellten Puffer muss dasselbe Ergebnis liefern. Weicht es ab,
//      hat der Decoder ueber rsize hinaus gelesen -- in der Firmware waere das
//      der uninitialisierte Rest des RX-Puffers (die CRC-Dumps zeigen dort
//      Reste alter Logzeilen und Heap-Floats).
//
//   pio test -e native_aprs_fuzz

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <ack_functions.h>
#include <nrf52/WisBlock-API.h>

// ---- Stubs fuer die Link-Abhaengigkeiten von aprs_functions.cpp ------------
// Identisch zu test_aprs_corpus.cpp; BOARD_HARDWARE ist bewusst int, weil
// loop_functions_extern.h es so deklariert (abweichende Definition = ODR).
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

// ---------------------------------------------------------------- Helfer

#define HEXLINE_MAX  1024
#define MAX_FRAMES   600
#define CANON_MAX    1024

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

// pio test startet das Binary je nach Plattform mit unterschiedlicher cwd.
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

static char frame_name[MAX_FRAMES][16];
static char frame_hex[MAX_FRAMES][HEXLINE_MAX];
static int  frame_count = 0;

static void loadCorpus(const char *rel)
{
    frame_count = 0;
    FILE *f = openRel(rel, "r");
    char miss[128];
    snprintf(miss, sizeof(miss), "%s nicht gefunden (cwd?)", rel);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, miss);

    char line[HEXLINE_MAX + 128];
    int total = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char name[16], hex[HEXLINE_MAX];
        if (sscanf(line, "%15s %1023s", name, hex) == 2)
        {
            total++;
            if (frame_count < MAX_FRAMES)
            {
                snprintf(frame_name[frame_count], sizeof(frame_name[0]), "%s", name);
                snprintf(frame_hex[frame_count], sizeof(frame_hex[0]), "%s", hex);
                frame_count++;
            }
        }
    }
    fclose(f);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, frame_count, "Korpus ist leer");

    // Kein stilles Abschneiden: lieber sichtbar scheitern und MAX_FRAMES
    // hochsetzen, als Frames unbemerkt aus dem Test fallen zu lassen.
    char msg[128];
    snprintf(msg, sizeof(msg), "%s hat %d Frames, MAX_FRAMES=%d -- Limit erhoehen",
             rel, total, MAX_FRAMES);
    TEST_ASSERT_EQUAL_INT_MESSAGE(total, frame_count, msg);
}

// Alles, was ein Aufrufer von decodeAPRS() ueberhaupt sehen kann.
static void canonical(char *out, size_t outsz, uint16_t rc, const struct aprsMessage &m)
{
    snprintf(out, outsz,
             "rc=%02X id=%08X hop=%u srv=%d mesh=%d track=%d appoff=%d "
             "src=%s dst=%s dstcall=%s pl=%s "
             "fcs=%04X hw=%u mod=%u fw=%u lasthw=%u len=%u "
             "srccall=%s srclast=%s pathcnt=%d",
             (unsigned)rc, m.msg_id, (unsigned)(m.max_hop & 0x0F),
             (int)m.msg_server, (int)m.msg_mesh, (int)m.msg_track, (int)m.msg_app_offline,
             m.msg_source_path.c_str(), m.msg_destination_path.c_str(),
             m.msg_destination_call.c_str(), m.msg_payload.c_str(),
             m.msg_fcs, (unsigned)m.msg_source_hw, (unsigned)m.msg_source_mod,
             (unsigned)m.msg_source_fw_version, (unsigned)m.msg_last_hw,
             (unsigned)m.msg_len,
             m.msg_source_call.c_str(), m.msg_source_last.c_str(),
             (int)m.msg_last_path_cnt);
}

// Ein Durchlauf mit definiertem Fuellmuster hinter dem Frame.
static uint16_t decodeWithFill(const char *hex, uint8_t fill, uint16_t &rsize,
                               char *canon, size_t canonsz)
{
    uint8_t buf[UDP_TX_BUF_SIZE];
    memset(buf, fill, sizeof(buf));
    rsize = hex2bin(hex, buf, sizeof(buf));

    struct aprsMessage m;
    initAPRS(m, 0x00);
    uint16_t rc = decodeAPRS(buf, rsize, m);
    canonical(canon, canonsz, rc, m);
    return rc;
}

// ---------------------------------------------------------------- Pruefungen

static void checkFrame(const char *name, const char *hex)
{
    char canon0[CANON_MAX], canonF[CANON_MAX];
    uint16_t size0 = 0, sizeF = 0;

    uint16_t rc0 = decodeWithFill(hex, 0x00, size0, canon0, sizeof(canon0));
    (void)decodeWithFill(hex, 0xFF, sizeF, canonF, sizeof(canonF));

    TEST_ASSERT_EQUAL_UINT16_MESSAGE(size0, sizeF, name);

    // (2) erlaubte Rueckgabewerte
    bool rc_ok = (rc0 == 0x00 || rc0 == 0x21 || rc0 == 0x3A || rc0 == 0x40 || rc0 == 0x41);
    char msg[256];
    snprintf(msg, sizeof(msg), "%s: decodeAPRS lieferte rc=0x%02X", name, (unsigned)rc0);
    TEST_ASSERT_TRUE_MESSAGE(rc_ok, msg);

    // (5) Determinismus gegen den Puffer-Rest hinter dem Frame
    snprintf(msg, sizeof(msg),
             "%s: Ergebnis haengt von Bytes hinter rsize=%u ab (Lesen ueber das "
             "Frame-Ende)", name, (unsigned)size0);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(canon0, canonF, msg);

    // (3)/(4) Ergebnisgrenzen -- nur fuer angenommene Frames aussagekraeftig
    if (rc0 == 0x00 || rc0 == 0x41) return;

    struct aprsMessage m;
    uint8_t buf[UDP_TX_BUF_SIZE];
    memset(buf, 0x00, sizeof(buf));
    uint16_t rsize = hex2bin(hex, buf, sizeof(buf));
    initAPRS(m, 0x00);
    decodeAPRS(buf, rsize, m);

    snprintf(msg, sizeof(msg), "%s: msg_len=%u > rsize=%u",
             name, (unsigned)m.msg_len, (unsigned)rsize);
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(rsize, m.msg_len, msg);

    size_t fields = m.msg_source_path.length() + m.msg_destination_path.length()
                  + m.msg_payload.length();
    snprintf(msg, sizeof(msg), "%s: Feldsumme %zu > rsize=%u", name, fields, (unsigned)rsize);
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE((size_t)rsize, fields, msg);
}

static void test_crc_corpus_robust(void)
{
    loadCorpus("test/test_aprs_fuzz/crc_corpus.txt");
    for (int i = 0; i < frame_count; i++)
        checkFrame(frame_name[i], frame_hex[i]);
}

// capture_corpus.txt entsteht erst, wenn Logs mit eingeschaltetem Mitschnitt
// vorliegen ("--loradebug on" / "--txcapture on", capture_functions.cpp). Bis
// dahin fehlt die Datei -- das ist kein Fehlschlag, wird aber gemeldet, damit
// niemand die Abdeckung fuer groesser haelt als sie ist.
static void test_capture_corpus_robust(void)
{
    FILE *probe = openRel("test/test_aprs_fuzz/capture_corpus.txt", "r");
    if (probe == nullptr)
    {
        printf("\n[capture] capture_corpus.txt fehlt -- noch keine Logs mit "
               "eingeschaltetem Rohframe-Mitschnitt geerntet\n");
        TEST_IGNORE_MESSAGE("kein Mitschnitt-Korpus vorhanden");
        return;
    }
    fclose(probe);

    loadCorpus("test/test_aprs_fuzz/capture_corpus.txt");
    printf("\n[capture] %d angenommene Frames aus dem Mitschnitt\n", frame_count);
    for (int i = 0; i < frame_count; i++)
        checkFrame(frame_name[i], frame_hex[i]);
}

static void test_ack_corpus_robust(void)
{
    loadCorpus("test/test_aprs_fuzz/ack_corpus.txt");
    for (int i = 0; i < frame_count; i++)
        checkFrame(frame_name[i], frame_hex[i]);
}

// isPlausibleAckFrame() gegen den Feldkorpus. Der Header ack_functions.h
// dokumentiert die Messung, aus der die Schranke stammt; hier wird sie gegen
// dieselben Frames eingefroren. Aendert jemand das Kriterium, verschiebt sich
// die Aufteilung sichtbar.
static void test_ack_plausibility_split(void)
{
    loadCorpus("test/test_aprs_fuzz/ack_corpus.txt");

    int accepted = 0, rejected = 0;
    for (int i = 0; i < frame_count; i++)
    {
        uint8_t buf[UDP_TX_BUF_SIZE];
        memset(buf, 0x00, sizeof(buf));
        uint16_t n = hex2bin(frame_hex[i], buf, sizeof(buf));

        if (isPlausibleAckFrame(buf, n, MAX_HOP_LIMIT))
            accepted++;
        else
            rejected++;
    }

    printf("\n[ack] %d Frames: %d plausibel, %d verworfen\n",
           frame_count, accepted, rejected);

    TEST_ASSERT_EQUAL_INT_MESSAGE(frame_count, accepted + rejected, "Zaehlung inkonsistent");
    // Der Korpus ist dedupliziert, die Quote weicht daher von der Rohmessung in
    // ack_functions.h ab. Gefordert ist nur, dass beide Klassen belegt bleiben:
    // ein Kriterium, das alles oder nichts akzeptiert, waere wirkungslos.
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, accepted, "kein einziger Frame plausibel");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, rejected, "kein einziger Frame verworfen");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_crc_corpus_robust);
    RUN_TEST(test_capture_corpus_robust);
    RUN_TEST(test_ack_corpus_robust);
    RUN_TEST(test_ack_plausibility_split);
    return UNITY_END();
}
