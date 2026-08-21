// Spec-abgeleitete Vektoren fuer decodeAPRS()/encodeAPRS() — Mechanismus 3
// des Test-Orakels: die Frames hier sind NICHT mitgeschnitten, sondern Byte
// fuer Byte aus docs/architecture/11-wire-format.md §1 konstruiert (eigener
// Builder, eigene FCS-Summe nach §1.3). Weicht Decoder oder Encoder vom
// Dokument ab, schlaegt die Suite fehl — Dokument und Code fechten es hier aus.
//
// Abgedeckt (Kapitelverweise = doc 11):
//   §1.1 Layout, LE-msg_id, Trailer optional beim Decoder / Pflicht beim Encoder
//   §1.2 Byte-5-Flags + Hop-Nibble (alle Kombinationen der 4 Flag-Bits)
//   §1.3 FCS = 16-bit-Bytesumme bis inkl. HW+MOD, big-endian; Fehlsumme -> Verwurf
//   §1.4 Pfad-/Ziel-Regeln: Rufzeichen-Regex, Gruppenziele, '*', 'H', "DE"-Verbot
//   §1.5 Kompakt-ACK 0x41 wird VOR der Mindestlaengen-Pruefung klassifiziert
//   §1.1 FW-Sub 0x7E/0x00 -> '#'; FW-Version 1..34 -> Verwurf
//
//   pio test -e native_aprs -f test_aprs_spec

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>   // Shim aus test/support: s_meshcom_settings

// In aprs_functions.cpp definiert, aber nicht im Header deklariert:
extern uint8_t shortVERSION(void);
extern char shortSUBVERSION(void);

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

// ------------------------------------------------------------------ Builder
//
// Baut ein Frame streng nach doc 11 §1.1 — bewusst unabhaengig von
// encodeAPRS(), damit Decoder UND Encoder gegen dieselbe dritte Instanz
// (das Dokument) geprueft werden.

enum TrailerMode { TRAILER_FULL, TRAILER_NONE };

static uint16_t buildFrame(uint8_t *out, uint8_t type, uint32_t id, uint8_t byte5,
                           const char *srcpath, const char *dst, const char *payload,
                           uint8_t hw, uint8_t mod,
                           TrailerMode trailer, uint8_t fw, uint8_t lasthw, uint8_t sub,
                           int fcs_delta)
{
    uint16_t n = 0;
    out[n++] = type;
    out[n++] = (uint8_t)(id & 0xFF);           // msg_id little-endian (§1.1)
    out[n++] = (uint8_t)((id >> 8) & 0xFF);
    out[n++] = (uint8_t)((id >> 16) & 0xFF);
    out[n++] = (uint8_t)((id >> 24) & 0xFF);
    out[n++] = byte5;                          // Flags + Hop (§1.2)
    memcpy(out + n, srcpath, strlen(srcpath)); n += (uint16_t)strlen(srcpath);
    out[n++] = '>';                            // Quellpfad-Terminator
    memcpy(out + n, dst, strlen(dst)); n += (uint16_t)strlen(dst);
    out[n++] = type;                           // Ziel-Terminator = Typ-Byte
    memcpy(out + n, payload, strlen(payload)); n += (uint16_t)strlen(payload);
    out[n++] = 0x00;                           // Payload-Terminator
    out[n++] = hw;
    out[n++] = mod;

    unsigned int fcs = 0;                      // §1.3: Summe 0..inkl. HW+MOD
    for (uint16_t i = 0; i < n; i++) fcs += out[i];
    fcs = (unsigned int)((int)fcs + fcs_delta);
    out[n++] = (uint8_t)((fcs >> 8) & 0xFF);   // big-endian
    out[n++] = (uint8_t)(fcs & 0xFF);

    if (trailer == TRAILER_FULL)
    {
        out[n++] = fw;
        out[n++] = lasthw;
        out[n++] = sub;
        out[n++] = 0x7E;
    }
    return n;
}

// --------------------------------------------------------- Decoder-Vektoren

static void test_spec_text_dm_alle_felder(void)
{
    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    // Text-DM: Server+Mesh, Hop 4, ein Relais im Pfad, Ack-Request {123
    uint16_t len = buildFrame(buf, 0x3A, 0xDEADBEEFUL, 0x80 | 0x10 | 0x04,
                              "DK5EN-90,DK5EN-98", "DK5EN-91", "Hallo Welt{123",
                              9, 0x88, TRAILER_FULL, 35, 0x80 | 43, 'p', 0);

    struct aprsMessage m;
    initAPRS(m, 0x00);
    uint16_t rc = decodeAPRS(buf, len, m);

    TEST_ASSERT_EQUAL_HEX16(0x3A, rc);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFUL, m.msg_id);
    TEST_ASSERT_EQUAL_UINT8(4, m.max_hop & 0x0F);
    TEST_ASSERT_TRUE(m.msg_server);
    TEST_ASSERT_TRUE(m.msg_mesh);
    TEST_ASSERT_FALSE(m.msg_track);
    TEST_ASSERT_FALSE(m.msg_app_offline);
    TEST_ASSERT_EQUAL_STRING("DK5EN-90,DK5EN-98", m.msg_source_path.c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-90", m.msg_source_call.c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-98", m.msg_source_last.c_str());
    TEST_ASSERT_EQUAL_INT(2, m.msg_last_path_cnt);
    TEST_ASSERT_EQUAL_STRING("DK5EN-91", m.msg_destination_path.c_str());
    TEST_ASSERT_EQUAL_STRING("DK5EN-91", m.msg_destination_call.c_str());
    TEST_ASSERT_EQUAL_STRING("Hallo Welt{123", m.msg_payload.c_str());
    TEST_ASSERT_EQUAL_UINT8(9, m.msg_source_hw);
    TEST_ASSERT_EQUAL_HEX8(0x88, m.msg_source_mod);
    TEST_ASSERT_EQUAL_UINT8(35, m.msg_source_fw_version);
    TEST_ASSERT_EQUAL_HEX8(0x80 | 43, m.msg_last_hw);
    TEST_ASSERT_EQUAL_UINT8('p', m.msg_source_fw_sub_version);

    // FCS-Wert im Struct muss der Bytesumme nach §1.3 entsprechen
    unsigned int expect_fcs = 0;
    for (uint16_t i = 0; i < (uint16_t)(len - 6); i++) expect_fcs += buf[i];
    TEST_ASSERT_EQUAL_HEX16((uint16_t)expect_fcs, m.msg_fcs);
}

static void test_spec_byte5_flags_und_hop(void)
{
    // Alle 16 Flag-Kombinationen der oberen 4 Bits + zwei Hop-Extreme (§1.2)
    for (int bits = 0; bits < 16; bits++)
    {
        uint8_t byte5 = (uint8_t)((bits << 4) | (bits == 0 ? 0x0F : 0x02));
        uint8_t buf[UDP_TX_BUF_SIZE] = {0};
        uint16_t len = buildFrame(buf, 0x21, 0x11223344UL, byte5,
                                  "OE1XAR-33", "*", "4825.35N\\01147.19E-Test",
                                  43, 0x88, TRAILER_FULL, 35, 0x80 | 43, 'p', 0);
        struct aprsMessage m;
        initAPRS(m, 0x00);
        uint16_t rc = decodeAPRS(buf, len, m);

        char ctx[32];
        snprintf(ctx, sizeof(ctx), "byte5=0x%02X", byte5);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x21, rc, ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(byte5 & 0x0F, m.max_hop & 0x0F, ctx);
        TEST_ASSERT_EQUAL_MESSAGE((bits & 0x8) != 0, m.msg_server, ctx);
        TEST_ASSERT_EQUAL_MESSAGE((bits & 0x4) != 0, m.msg_track, ctx);
        TEST_ASSERT_EQUAL_MESSAGE((bits & 0x2) != 0, m.msg_app_offline, ctx);
        TEST_ASSERT_EQUAL_MESSAGE((bits & 0x1) != 0, m.msg_mesh, ctx);
    }
}

static void test_spec_gruppenziele_und_sonderziele(void)
{
    // §1.4: Gruppen numerisch 1..99999 (+Sonderfall 100001), '*', 'H'
    TEST_ASSERT_EQUAL_INT(9999,   CheckGroup("9999"));
    TEST_ASSERT_EQUAL_INT(20,     CheckGroup("20"));
    TEST_ASSERT_EQUAL_INT(100001, CheckGroup("100001"));
    TEST_ASSERT_EQUAL_INT(0,      CheckGroup("0"));       // ausserhalb 1..99999
    TEST_ASSERT_EQUAL_INT(0,      CheckGroup("123456"));  // > 99999
    TEST_ASSERT_EQUAL_INT(0,      CheckGroup("DK5EN"));   // nicht numerisch

    static const char *ok_dst[] = { "9999", "20", "*", "H", "DK5EN-91" };
    for (const char *dst : ok_dst)
    {
        uint8_t buf[UDP_TX_BUF_SIZE] = {0};
        uint16_t len = buildFrame(buf, 0x3A, 0x01020304UL, 0x14,
                                  "DK5EN-90", dst, "Gruppen-Test",
                                  9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);
        struct aprsMessage m;
        initAPRS(m, 0x00);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x3A, decodeAPRS(buf, len, m), dst);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(dst, m.msg_destination_call.c_str(), dst);
    }
}

static void test_spec_trailer_optional_beim_decoder(void)
{
    // §1.1: FW/LASTHW/FW-Sub/0x7E duerfen fehlen — Decoder behaelt dann
    // seine Initialwerte (eigene Version/Hardware) bei.
    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = buildFrame(buf, 0x3A, 0x0A0B0C0DUL, 0x12,
                              "DK5EN-90", "*", "ohne Trailer",
                              9, 0x88, TRAILER_NONE, 0, 0, 0, 0);
    struct aprsMessage m;
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16(0x3A, decodeAPRS(buf, len, m));
    TEST_ASSERT_EQUAL_STRING("ohne Trailer", m.msg_payload.c_str());
    TEST_ASSERT_EQUAL_UINT8(shortVERSION(), m.msg_source_fw_version);
    TEST_ASSERT_EQUAL_HEX8(0x80 | BOARD_HARDWARE, m.msg_last_hw);
    TEST_ASSERT_EQUAL_UINT8(shortSUBVERSION(), m.msg_source_fw_sub_version);
}

static void test_spec_fw_sub_platzhalter(void)
{
    // §1.1: FW-Sub-Bytes 0x7E und 0x00 werden als '#' gelesen
    static const uint8_t subs[] = { 0x7E, 0x00 };
    for (uint8_t sub : subs)
    {
        uint8_t buf[UDP_TX_BUF_SIZE] = {0};
        uint16_t len = buildFrame(buf, 0x3A, 0x22334455UL, 0x14,
                                  "DK5EN-90", "*", "Sub-Test",
                                  9, 0x88, TRAILER_FULL, 35, 0x80 | 9, sub, 0);
        struct aprsMessage m;
        initAPRS(m, 0x00);
        TEST_ASSERT_EQUAL_HEX16(0x3A, decodeAPRS(buf, len, m));
        TEST_ASSERT_EQUAL_UINT8('#', m.msg_source_fw_sub_version);
    }
}

static void test_spec_kompakt_ack_vor_laengenpruefung(void)
{
    // §1.5: das 12-Byte-Binaer-ACK ist KUERZER als die Mindestlaenge 16 —
    // die 0x41-Klassifikation muss vor der Laengenpruefung greifen.
    uint8_t ack[12] = { 0x41,
                        0x78, 0x56, 0x34, 0x12,   // eigene msg_id LE
                        0x80 | 0x04,              // Server-Flag | Hop
                        0xEF, 0xBE, 0xAD, 0xDE,   // bestaetigte msg_id LE
                        0x01,                     // Ack-Level Gateway
                        0x00 };
    struct aprsMessage m;
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16(0x41, decodeAPRS(ack, sizeof(ack), m));

    // §1.5: 0x3C (LoRa-APRS) wird nicht dekodiert
    uint8_t lora_aprs[20] = { 0x3C, 'O', 'E', '1', 'K', 'B', 'C' };
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16(0x00, decodeAPRS(lora_aprs, sizeof(lora_aprs), m));
}

static void test_spec_verwurfregeln(void)
{
    struct aprsMessage m;
    uint8_t buf[UDP_TX_BUF_SIZE];

    // (a) kuerzer als 16 Bytes
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x3A;
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, 15, m), "min. Laenge");

    // (b) FCS-Summe um 1 daneben
    memset(buf, 0, sizeof(buf));
    uint16_t len = buildFrame(buf, 0x3A, 0x01UL, 0x14, "DK5EN-90", "*", "FCS kaputt",
                              9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', +1);
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, len, m), "FCS");

    // (c) Quell-Rufzeichen faellt durch die Regex; (d) "DE" ist explizit verboten
    static const char *bad_src[] = { "QQQQ", "DE" };
    for (const char *src : bad_src)
    {
        memset(buf, 0, sizeof(buf));
        len = buildFrame(buf, 0x3A, 0x01UL, 0x14, src, "*", "Rufzeichen kaputt",
                         9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);
        initAPRS(m, 0x00);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, len, m), src);
    }

    // (e) Ziel weder Gruppe noch gueltiges Rufzeichen
    memset(buf, 0, sizeof(buf));
    len = buildFrame(buf, 0x3A, 0x01UL, 0x14, "DK5EN-90", "QQQQ", "Ziel kaputt",
                     9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, len, m), "Ziel-Regex");

    // (f) '>'-Terminator fehlt
    memset(buf, 0, sizeof(buf));
    len = buildFrame(buf, 0x3A, 0x01UL, 0x14, "DK5EN-90", "*", "kein Pfeil",
                     9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);
    buf[6 + 8] = '-';   // das '>' hinter "DK5EN-90" ueberschreiben
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, len, m), "'>' fehlt");

    // (g) Ziel-Terminator (Typ-Byte-Wiederholung) fehlt
    memset(buf, 0, sizeof(buf));
    len = buildFrame(buf, 0x3A, 0x01UL, 0x14, "DK5EN-90", "*", "kein Doppelpunkt",
                     9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);
    buf[6 + 8 + 1 + 1] = 'x';   // Typ-Byte hinter "*" ueberschreiben
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, len, m), "Ziel-Terminator fehlt");

    // (h) Payload-Terminator 0x00 fehlt (Frame vor dem Terminator abgeschnitten)
    memset(buf, 0, sizeof(buf));
    len = buildFrame(buf, 0x3A, 0x01UL, 0x14, "DK5EN-90", "*", "abgeschnitten....",
                     9, 0x88, TRAILER_NONE, 0, 0, 0, 0);
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, (uint16_t)(len - 5), m),
                                    "Payload-Terminator fehlt");

    // (i) Trailer (HW/MOD/FCS) abgeschnitten
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, (uint16_t)(len - 1), m),
                                    "HW/MOD/FCS abgeschnitten");

    // (j) FW-Version 1..34 wird verworfen (§1.1), 35 akzeptiert
    memset(buf, 0, sizeof(buf));
    len = buildFrame(buf, 0x3A, 0x01UL, 0x14, "DK5EN-90", "*", "alte Firmware",
                     9, 0x88, TRAILER_FULL, 34, 0x80 | 9, 'p', 0);
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, len, m), "FW 34");
    memset(buf, 0, sizeof(buf));
    len = buildFrame(buf, 0x3A, 0x01UL, 0x14, "DK5EN-90", "*", "alte Firmware",
                     9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x3A, decodeAPRS(buf, len, m), "FW 35");

    // (k) unbekanntes Typ-Byte
    memset(buf, 0, sizeof(buf));
    len = buildFrame(buf, 0x25, 0x01UL, 0x14, "DK5EN-90", "*", "falscher Typ",
                     9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);
    initAPRS(m, 0x00);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x00, decodeAPRS(buf, len, m), "Typ 0x25");
}

// --------------------------------------------------------- Encoder-Vektor

static void test_spec_encoder_bytegenau(void)
{
    // encodeAPRS() muss exakt das Layout aus doc 11 §1.1 schreiben —
    // verglichen wird gegen den unabhaengigen Builder dieses Tests.
    struct aprsMessage m;
    initAPRS(m, ':');
    m.msg_id = 0xCAFEBABEUL;
    m.max_hop = 4;
    m.msg_server = true;
    m.msg_track = false;
    m.msg_app_offline = false;
    // bMESH ist true -> Encoder setzt 0x10 (§1.2)
    m.msg_source_path = "DK5EN-90";
    m.msg_destination_path = "9999";
    m.msg_payload = "Spec-Encoder-Test";
    m.msg_source_hw = 9;
    m.msg_source_mod = 0x88;
    m.msg_source_fw_version = 35;
    m.msg_last_hw = 0x80 | 9;
    m.msg_source_fw_sub_version = 'p';

    uint8_t enc[UDP_TX_BUF_SIZE] = {0};
    uint16_t enc_len = encodeAPRS(enc, m);

    uint8_t expect[UDP_TX_BUF_SIZE] = {0};
    uint16_t expect_len = buildFrame(expect, 0x3A, 0xCAFEBABEUL, 0x80 | 0x10 | 0x04,
                                     "DK5EN-90", "9999", "Spec-Encoder-Test",
                                     9, 0x88, TRAILER_FULL, 35, 0x80 | 9, 'p', 0);

    TEST_ASSERT_EQUAL_UINT16(expect_len, enc_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, enc, expect_len);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_spec_text_dm_alle_felder);
    RUN_TEST(test_spec_byte5_flags_und_hop);
    RUN_TEST(test_spec_gruppenziele_und_sonderziele);
    RUN_TEST(test_spec_trailer_optional_beim_decoder);
    RUN_TEST(test_spec_fw_sub_platzhalter);
    RUN_TEST(test_spec_kompakt_ack_vor_laengenpruefung);
    RUN_TEST(test_spec_verwurfregeln);
    RUN_TEST(test_spec_encoder_bytegenau);
    return UNITY_END();
}
