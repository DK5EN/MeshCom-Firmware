// Interop-Orakel fuer encodeAPRS(): baut Frames aus mitgeloggten Feldern neu
// auf und vergleicht Laenge und Pruefsumme mit den Werten, die der ABSENDER
// auf die Luftschnittstelle gelegt hat.
//
// Warum das kein Zirkelschluss ist:
// Die Logzeilen von printBuffer_aprs() sind die AUSGABE unseres Decoders --
// gegen den Decoder selbst waeren sie wertlos. Aber `msg_fcs` liest der
// Decoder aus dem Frame (aprs_functions.cpp:416) und verwirft den Frame, wenn
// die nachgerechnete Bytesumme nicht passt (:427). Jeder geloggte Frame trug
// also eine vom Absender berechnete, von uns verifizierte Pruefsumme ueber die
// echten Wire-Bytes. Reproduziert encodeAPRS() diese Summe nicht, hat unser
// Encoder eine andere Byteabfolge erzeugt als der Absender -- unabhaengig
// nachweisbar, ohne zweites Binary und ohne Handvektoren.
//
// Dasselbe gilt fuer die Laenge: `msg_len` ist der Offset, an dem der Decoder
// den Frame enden sah (:483), nicht die Empfangsgroesse.
//
// Korpus: reencode_vectors.txt, geerntet mit tools/logharvest.py aus 32,7 h Log
// von DG0OPK-11/-12/-13 und DJ8MEH-41 (23.-25.08.2026). 42.110 verschiedene
// Vektoren im Rohbestand, davon je einer pro Verhaltenssignatur eingecheckt.
// Fremdfirmware ist eingeschlossen (HW 3, 4, 9, 10, 12, 39, 41, 42, 43, 46,
// 51; FW 0 und 35) -- Interop-Abdeckung, die der Bench-RAK4631 nie erzeugt.
//
// Zwei Freiheitsgrade, die die Logzeile nicht hergibt, werden durchprobiert:
//   - msg_app_offline (Byte-5-Bit 0x20) druckt printBuffer_aprs() nicht.
//   - Das Mesh-Bit setzt encodeAPRS() aus dem globalen bMESH, nicht aus
//     aprsmsg.msg_mesh (Doc 11 §1.2) -- bMESH wird je Vektor nachgezogen.
// Ein Vektor gilt als reproduziert, wenn EINE der Kombinationen passt.
//
//   pio test -e native_aprs_fuzz -f test_aprs_reencode

#include <unity.h>

#include <stdio.h>
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

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- Helfer

#define HEXFIELD_MAX 1024

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

// Hex-Feld -> String. '-' steht fuer den leeren String.
static String unhex(const char *hex)
{
    String s;
    if (hex[0] == '-' && hex[1] == 0) return s;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2)
    {
        int h = nib(hex[i]), l = nib(hex[i + 1]);
        if (h < 0 || l < 0) break;
        s.concat((char)((h << 4) | l));
    }
    return s;
}

struct Vector
{
    char     name[16];
    char     tag[4];
    unsigned type, id, hop, srv, trk, mesh, hw, mod, fcs, fw, lasthw, fwsub;
    unsigned explen;
    String   src, dst, payload;
};

static bool parseLine(const char *line, Vector &v)
{
    char srch[HEXFIELD_MAX], dsth[HEXFIELD_MAX], plh[HEXFIELD_MAX];
    int n = sscanf(line,
                   "%15s %3s %x %x %x %u %u %x %x %x %x %x %x %x %u "
                   "%1023s %1023s %1023s",
                   v.name, v.tag, &v.type, &v.id, &v.hop, &v.srv, &v.trk, &v.mesh,
                   &v.hw, &v.mod, &v.fcs, &v.fw, &v.lasthw, &v.fwsub, &v.explen,
                   srch, dsth, plh);
    if (n != 18) return false;
    v.src     = unhex(srch);
    v.dst     = unhex(dsth);
    v.payload = unhex(plh);
    return true;
}

// Baut die Nachricht so auf, wie decodeAPRS() sie geliefert haette, und
// enkodiert sie. Gibt die Encoder-Laenge zurueck, fcs_out die Bytesumme.
static uint16_t reencode(const Vector &v, bool app_offline, unsigned &fcs_out)
{
    struct aprsMessage m;
    initAPRS(m, (char)v.type);

    m.payload_type            = (char)v.type;
    m.msg_id                  = v.id;
    m.max_hop                 = (uint8_t)v.hop;
    m.msg_server              = v.srv != 0;
    m.msg_track               = v.trk != 0;
    m.msg_app_offline         = app_offline;
    m.msg_mesh                = v.mesh != 0;
    m.msg_source_path         = v.src;
    m.msg_destination_path    = v.dst;
    m.msg_payload             = v.payload;
    m.msg_source_hw           = (uint8_t)v.hw;
    m.msg_source_mod          = (uint8_t)v.mod;
    m.msg_source_fw_version   = (uint8_t)v.fw;
    m.msg_last_hw             = (uint8_t)v.lasthw;
    m.msg_source_fw_sub_version = (char)v.fwsub;

    // Das Mesh-Bit zieht encodeAPRS() aus dem globalen bMESH.
    bMESH = m.msg_mesh;

    uint8_t buf[UDP_TX_BUF_SIZE];
    memset(buf, 0x00, sizeof(buf));
    uint16_t len = encodeAPRS(buf, m);
    fcs_out = m.msg_fcs;
    return len;
}

// ---------------------------------------------------------------- Pruefung

// Eingefrorene Abweichungszahlen fuer den eingecheckten Korpus.
//
// FCS: 0. encodeAPRS() reproduziert die Bytesumme JEDES real gehoerten
// Frames -- ueber alle Fremdfirmware-Staende im Korpus hinweg.
//
// Laenge: die einzige beobachtete Klasse sind Absender, die die
// 0x7E-Endemarke nicht schreiben. Der Decoder liest den Trailer "falls
// vorhanden" (aprs_functions.cpp:446-481) und zaehlt dann ein Byte weniger,
// der Encoder schreibt sie immer (:1114). Das ist dokumentiertes Verhalten
// (Doc 11 §1.1), kein Fehler -- aber es wird hier gezaehlt, damit ein
// Anwachsen auffaellt.
#define EXPECTED_FCS_MISMATCH   0
#define EXPECTED_LEN_MISMATCH   1

static void test_reencode_gegen_wire_werte(void)
{
    FILE *f = openRel("test/test_aprs_reencode/reencode_vectors.txt", "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, "reencode_vectors.txt nicht gefunden (cwd?)");

    int checked = 0, skipped_tx = 0, unparsable = 0;
    int fcs_bad = 0, len_bad = 0;
    int shown = 0;

    char line[4 * HEXFIELD_MAX];
    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        Vector v;
        if (!parseLine(line, v)) { unparsable++; continue; }

        // tag=tx stammt aus unserem eigenen Encoder -- als Orakel wertlos.
        if (strcmp(v.tag, "rx") != 0) { skipped_tx++; continue; }

        bool fcs_ok = false, len_ok = false;
        unsigned got_fcs = 0;
        uint16_t got_len = 0;
        for (int ao = 0; ao < 2 && !(fcs_ok && len_ok); ao++)
        {
            unsigned fcs = 0;
            uint16_t len = reencode(v, ao != 0, fcs);
            if (fcs == v.fcs)    fcs_ok = true;
            if (len == v.explen) len_ok = true;
            got_fcs = fcs;
            got_len = len;
        }

        checked++;
        if (!fcs_ok) fcs_bad++;
        if (!len_ok) len_bad++;

        if ((!fcs_ok || !len_ok) && shown < 10)
        {
            shown++;
            printf("  %s type=%c fw=%u hw=%u  fcs wire=%04X ours=%04X  "
                   "len wire=%u ours=%u  src=%s dst=%s\n",
                   v.name, (char)v.type, v.fw, v.hw, v.fcs, got_fcs,
                   v.explen, (unsigned)got_len,
                   v.src.c_str(), v.dst.c_str());
        }
    }
    fclose(f);

    printf("\n[reencode] %d Vektoren geprueft (%d tx uebersprungen, %d unlesbar)\n",
           checked, skipped_tx, unparsable);
    printf("[reencode] FCS: %d abweichend, Laenge: %d abweichend\n", fcs_bad, len_bad);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, unparsable,
                                  "Vektordatei nicht lesbar -- Format geaendert?");
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, checked, "keine rx-Vektoren im Korpus");

    TEST_ASSERT_EQUAL_INT_MESSAGE(EXPECTED_FCS_MISMATCH, fcs_bad,
        "encodeAPRS() erzeugt eine andere Bytesumme als der Absender -- "
        "Encoder und Wire-Format sind auseinandergelaufen");
    TEST_ASSERT_EQUAL_INT_MESSAGE(EXPECTED_LEN_MISMATCH, len_bad,
        "Laengenabweichungen haben sich veraendert (bekannte Klasse: fehlende "
        "0x7E-Endemarke beim Absender)");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_reencode_gegen_wire_werte);
    return UNITY_END();
}
