// U7 characterization test for the APRS frame epilogue -- the trailer that
// follows the payload terminator and carries the sender's hardware/firmware
// identification plus the FCS. Pattern follows test_aprs_spec.cpp (an
// independent byte-builder, not the encoder itself, as a third instance) and
// test_aprs_decode.cpp (the link stubs this env needs).
//
// The epilogue as established by reading encodeStartAPRS()/encodePayloadAPRS()/
// encodeAPRS() and decodeAPRS()'s tail handling in src/aprs_functions.cpp:
//
//   [payload] 0x00 HW MOD FCS_HI FCS_LO FW LASTHW SUB 0x7E
//             \___________________________________________/
//                          9 bytes, always written by
//                          encodeAPRS() -- never optional
//                          on the TX side (only the RX side
//                          tolerates a missing/short trailer).
//
//   HW      = aprsmsg.msg_source_hw                          (1 byte, verbatim)
//   MOD     = aprsmsg.msg_source_mod                         (1 byte, verbatim)
//   FCS     = 16-bit sum of every byte from the type byte (index 0) through
//             MOD inclusive, big-endian                      (2 bytes)
//   FW      = aprsmsg.msg_source_fw_version                  (1 byte, verbatim)
//   LASTHW  = aprsmsg.msg_last_hw                             (1 byte, verbatim)
//   SUB     = aprsmsg.msg_source_fw_sub_version, EXCEPT the encoder
//             substitutes 0x23 ('#') whenever the field is exactly 0x00
//                                                             (1 byte, conditional)
//   0x7E    = fixed terminator                                (1 byte)
//
// Decoder-side conditionals (aprs_functions.cpp ~421-491) that only matter
// once a trailer is present at all (trailer-present-or-absent itself is
// already exhaustively covered by test_spec_trailer_optional_beim_decoder in
// test_aprs_spec.cpp, so this file does not repeat it):
//   * FW/LASTHW/SUB are read one at a time, each gated on `inext < rsize` --
//     a trailer can be partially present.
//   * SUB byte == 0x7E is read back as '#' (not as the raw byte) *and* still
//     consumes it as if it were the SUB field; the decoder then looks for a
//     *second* 0x7E as the real terminator. Fed the encoder's own output this
//     never bites UNLESS msg_source_fw_sub_version itself is 0x7E, in which
//     case the encoder does not substitute (only 0x00 triggers substitution)
//     and the wire ends up with two consecutive 0x7E bytes. See
//     test_epilogue_sub_0x7e_collision below for what that does to a round
//     trip -- it is real drift, not a hypothetical.
//   * fw_version in (0, 35) is discarded post-hoc (line 496); fw_version==0
//     is NOT discarded (the check is `> 0 && < 35`), which is its own
//     boundary worth pinning.
//
//   pio test -e native_aprs -f test_aprs_epilogue

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <Arduino.h>
#include <aprs_functions.h>
#include <nrf52/WisBlock-API.h>   // stub from test/support: s_meshcom_settings

// ---- Link-dependency stubs for aprs_functions.cpp, same as the sibling
// suites in this env (test_aprs_decode.cpp / test_aprs_spec.cpp). ----------
s_meshcom_settings meshcom_settings;
bool bDisplayInfo = false;
bool bDisplayCont = false;
bool bLORADEBUG = false;
bool bMESH = true;
int BOARD_HARDWARE = 9;   // int, not uint8_t: ODR rationale in test_txring.cpp (Verdict Finding 4)
int getMOD(void) { return 3; }
void printAsciiBuffer(unsigned char *buf, int len) { (void)buf; (void)len; }

void setUp(void) {}
void tearDown(void) {}

// --------------------------------------------------------------------------
// Independent frame builder -- deliberately NOT calling encodeAPRS(). Mirrors
// the layout documented above so the comparison in the byte-exact tests below
// is against a second implementation, not against the encoder agreeing with
// itself. `sub` is written verbatim (no 0x00->'#' substitution) -- callers
// that want to exercise the substitution use a raw expected array instead
// (see test_epilogue_sub_substitution / test_epilogue_sub_0x7e_collision).
// --------------------------------------------------------------------------
static uint16_t buildExpectedFrame(uint8_t *out, uint8_t type, uint32_t id, uint8_t byte5,
                                    const char *srcpath, const char *dst, const char *payload,
                                    uint8_t hw, uint8_t mod, uint8_t fw, uint8_t lasthw, uint8_t sub)
{
    uint16_t n = 0;
    out[n++] = type;
    out[n++] = (uint8_t)(id & 0xFF);
    out[n++] = (uint8_t)((id >> 8) & 0xFF);
    out[n++] = (uint8_t)((id >> 16) & 0xFF);
    out[n++] = (uint8_t)((id >> 24) & 0xFF);
    out[n++] = byte5;
    memcpy(out + n, srcpath, strlen(srcpath)); n += (uint16_t)strlen(srcpath);
    out[n++] = '>';
    memcpy(out + n, dst, strlen(dst)); n += (uint16_t)strlen(dst);
    out[n++] = type;                           // destination terminator = type byte
    memcpy(out + n, payload, strlen(payload)); n += (uint16_t)strlen(payload);
    out[n++] = 0x00;                           // ---- epilogue starts here ----

    out[n++] = hw;
    out[n++] = mod;

    unsigned int fcs = 0;                      // sum over [0, n) i.e. through MOD
    for (uint16_t i = 0; i < n; i++) fcs += out[i];
    out[n++] = (uint8_t)((fcs >> 8) & 0xFF);
    out[n++] = (uint8_t)(fcs & 0xFF);

    out[n++] = fw;
    out[n++] = lasthw;
    out[n++] = sub;
    out[n++] = 0x7E;
    return n;
}

// Slices the last 9 bytes of an encoded frame and checks them against the
// documented epilogue layout field by field.
static void assertEpilogue(const uint8_t *buf, uint16_t len, uint8_t hw, uint8_t mod,
                            uint16_t fcs, uint8_t fw, uint8_t lasthw, uint8_t sub,
                            const char *ctx)
{
    TEST_ASSERT_TRUE_MESSAGE(len >= 9, ctx);
    const uint8_t *e = buf + len - 9;
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00, e[0], ctx);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(hw, e[1], ctx);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(mod, e[2], ctx);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(fcs, (uint16_t)((e[3] << 8) | e[4]), ctx);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(fw, e[5], ctx);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(lasthw, e[6], ctx);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(sub, e[7], ctx);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x7E, e[8], ctx);
}

// Builds a plain aprsMessage with the given payload type and epilogue
// values, all fields our own (DK5EN-90), destination the bench group 9999
// (never "*").
static void buildMessage(struct aprsMessage &m, char msgType, uint32_t id,
                          const char *payload, uint8_t hw, uint8_t mod,
                          uint8_t fw, uint8_t lasthw, char sub)
{
    initAPRS(m, msgType);
    m.msg_id = id;
    m.max_hop = 4;
    m.msg_server = true;
    m.msg_source_path = "DK5EN-90";
    m.msg_destination_path = "9999";
    m.msg_payload = payload;
    m.msg_source_hw = hw;
    m.msg_source_mod = mod;
    m.msg_source_fw_version = fw;
    m.msg_last_hw = lasthw;
    m.msg_source_fw_sub_version = sub;
}

// ---------------------------------------------------------------- Cases ---

// Byte-exact epilogue for all three frame types encodeAPRS() accepts: text
// (0x3A), position (0x21) and "hey" (0x40, ASCII '@').
static void test_epilogue_bytegenau_alle_typen(void)
{
    struct { char msgType; uint8_t typeByte; const char *payload; const char *ctx; } cases[] = {
        { ':', 0x3A, "Hallo Welt",                    "text 0x3A" },
        { '!', 0x21, "4825.35N/01147.19E-Test",       "position 0x21" },
        { '@', 0x40, "3,-72,5.5;",                     "hey 0x40" },
    };

    for (auto &c : cases)
    {
        struct aprsMessage m;
        buildMessage(m, c.msgType, 0x11223344UL, c.payload, 9, 0x88, 35, 0x80 | 9, 'p');

        uint8_t enc[UDP_TX_BUF_SIZE] = {0};
        uint16_t enc_len = encodeAPRS(enc, m);

        uint8_t expect[UDP_TX_BUF_SIZE] = {0};
        // byte5: server(0x80) | mesh(0x10, bMESH is true in this env) | hop(4)
        uint16_t expect_len = buildExpectedFrame(expect, c.typeByte, 0x11223344UL,
                                                  0x80 | 0x10 | 0x04,
                                                  "DK5EN-90", "9999", c.payload,
                                                  9, 0x88, 35, 0x80 | 9, 'p');

        TEST_ASSERT_EQUAL_UINT16_MESSAGE(expect_len, enc_len, c.ctx);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expect, enc, expect_len, c.ctx);

        unsigned int fcs = 0;
        for (uint16_t i = 0; i < (uint16_t)(enc_len - 6); i++) fcs += enc[i];
        assertEpilogue(enc, enc_len, 9, 0x88, (uint16_t)fcs, 35, 0x80 | 9, 'p', c.ctx);
    }
}

// The FCS hand-computed by hand, not by re-running the code's own loop --
// per the plan, at least one vector must not merely agree with the code's
// own arithmetic.
//
// Frame: type 0x3A, msg_id=0x00000001, byte5=0x94 (server|mesh|hop4),
// src "DK5EN-90", dst "9999", payload "Hi", HW=0x09, MOD=0x83.
//
// Bytes summed (index: value, decimal):
//   0:0x3A=58  1:0x01=1  2:0x00=0  3:0x00=0  4:0x00=0  5:0x94=148
//   6:'D'=0x44=68  7:'K'=0x4B=75  8:'5'=0x35=53  9:'E'=0x45=69
//  10:'N'=0x4E=78 11:'-'=0x2D=45 12:'9'=0x39=57 13:'0'=0x30=48
//  14:'>'=0x3E=62
//  15:'9'=0x39=57 16:'9'=0x39=57 17:'9'=0x39=57 18:'9'=0x39=57
//  19:0x3A=58 (destination terminator = type byte)
//  20:'H'=0x48=72 21:'i'=0x69=105
//  22:0x00=0 (payload terminator)
//  23:HW=0x09=9  24:MOD=0x83=131
// Running sum: 58+1+0+0+0+148=207; +68+75+53+69+78+45+57+48=207+493=700;
// +62=762; +57*4=228 -> 990; +58=1048; +72+105=1225; +0=1225; +9=1234;
// +131=1365 = 0x0555.
static void test_epilogue_fcs_von_hand(void)
{
    struct aprsMessage m;
    buildMessage(m, ':', 0x00000001UL, "Hi", 0x09, 0x83, 35, 0x80 | 9, 'p');
    m.msg_destination_path = "9999";

    uint8_t enc[UDP_TX_BUF_SIZE] = {0};
    uint16_t enc_len = encodeAPRS(enc, m);

    static const uint8_t expect[] = {
        0x3A, 0x01, 0x00, 0x00, 0x00, 0x94,
        'D', 'K', '5', 'E', 'N', '-', '9', '0',
        '>',
        '9', '9', '9', '9',
        0x3A,
        'H', 'i',
        0x00,             // payload terminator
        0x09,             // HW
        0x83,             // MOD
        0x05, 0x55,       // FCS = 1365 = 0x0555, hand-summed above
        35,               // FW
        0x80 | 9,         // LASTHW
        'p',              // SUB
        0x7E,             // terminator
    };

    TEST_ASSERT_EQUAL_UINT16(sizeof(expect), enc_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, enc, sizeof(expect));
    TEST_ASSERT_EQUAL_HEX16(0x0555, (uint16_t)m.msg_fcs);
}

// Round trip: encodeAPRS() -> decodeAPRS() must return the epilogue fields
// unchanged, for all three frame types.
static void test_epilogue_roundtrip_alle_typen(void)
{
    struct { char msgType; const char *payload; const char *ctx; } cases[] = {
        { ':', "Roundtrip Text",              "text 0x3A" },
        { '!', "4825.35N/01147.19E-RT",       "position 0x21" },
        { '@', "2,-80,3;",                     "hey 0x40" },
    };

    for (auto &c : cases)
    {
        struct aprsMessage tx;
        buildMessage(tx, c.msgType, 0x0BADF00DUL, c.payload, 43, 0x86, 36, 0x80 | 43, 'q');

        uint8_t buf[UDP_TX_BUF_SIZE] = {0};
        uint16_t len = encodeAPRS(buf, tx);
        TEST_ASSERT_TRUE_MESSAGE(len > 0, c.ctx);

        struct aprsMessage rx;
        initAPRS(rx, 0x00);
        uint16_t rc = decodeAPRS(buf, len, rx);

        TEST_ASSERT_EQUAL_HEX16_MESSAGE((uint8_t)c.msgType, rc, c.ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(43, rx.msg_source_hw, c.ctx);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x86, rx.msg_source_mod, c.ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(36, rx.msg_source_fw_version, c.ctx);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE('q', rx.msg_source_fw_sub_version, c.ctx);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x80 | 43, rx.msg_last_hw, c.ctx);
    }
}

// Conditional #1: SUB field == 0x00 is written as '#' (0x23) on the wire,
// not verbatim -- the ONLY value the encoder substitutes.
static void test_epilogue_sub_substitution(void)
{
    struct aprsMessage m;
    buildMessage(m, ':', 0x42UL, "Sub-Test", 9, 0x88, 35, 0x80 | 9, (char)0x00);

    uint8_t enc[UDP_TX_BUF_SIZE] = {0};
    uint16_t enc_len = encodeAPRS(enc, m);

    // epilogue[7] is the SUB byte (see assertEpilogue layout)
    TEST_ASSERT_EQUAL_HEX8(0x23, enc[enc_len - 9 + 7]);

    struct aprsMessage rx;
    initAPRS(rx, 0x00);
    uint16_t rc = decodeAPRS(enc, enc_len, rx);
    TEST_ASSERT_EQUAL_HEX16(0x3A, rc);
    TEST_ASSERT_EQUAL_UINT8('#', rx.msg_source_fw_sub_version);
}

// Conditional #2 -- real drift risk, not hypothetical: SUB == 0x7E is NOT
// substituted by the encoder (only 0x00 is), so the wire ends up with two
// consecutive 0x7E bytes (SUB, then the real terminator). The decoder's
// "SUB byte == 0x7E" branch (aprs_functions.cpp ~470-474) then reads the
// first 0x7E back as '#' and consumes it as the SUB field, then separately
// consumes the second 0x7E as the terminator. Net effect: an input SUB of
// 0x7E is NOT round-tripped -- it comes back as '#'. This is what IS, pinned
// so a future change to either side has to say so.
static void test_epilogue_sub_0x7e_collision(void)
{
    struct aprsMessage m;
    buildMessage(m, ':', 0x43UL, "Collision-Test", 9, 0x88, 35, 0x80 | 9, (char)0x7E);

    uint8_t enc[UDP_TX_BUF_SIZE] = {0};
    uint16_t enc_len = encodeAPRS(enc, m);

    // Two consecutive 0x7E at the tail: SUB (unsubstituted) then terminator.
    TEST_ASSERT_EQUAL_HEX8(0x7E, enc[enc_len - 2]);
    TEST_ASSERT_EQUAL_HEX8(0x7E, enc[enc_len - 1]);

    struct aprsMessage rx;
    initAPRS(rx, 0x00);
    uint16_t rc = decodeAPRS(enc, enc_len, rx);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x3A, rc, "frame must still decode, not be discarded");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE('#', rx.msg_source_fw_sub_version,
                                    "0x7E SUB does not round-trip -- decoded as '#'");
}

// Conditional #3: fw_version discard window is `> 0 && < 35` (line 496).
// 34 is discarded, 35 is accepted, and -- the boundary worth pinning -- 0 is
// ALSO accepted (the check does not fire for the "unknown" placeholder).
static void test_epilogue_fw_version_grenzen(void)
{
    struct { uint8_t fw; uint16_t expect_rc; const char *ctx; } cases[] = {
        { 34, 0x00, "fw 34 discarded" },
        { 35, 0x3A, "fw 35 accepted" },
        { 0,  0x3A, "fw 0 accepted (check requires >0)" },
    };

    for (auto &c : cases)
    {
        struct aprsMessage tx;
        buildMessage(tx, ':', 0x99UL, "FW-Grenze", 9, 0x88, c.fw, 0x80 | 9, 'p');

        uint8_t buf[UDP_TX_BUF_SIZE] = {0};
        uint16_t len = encodeAPRS(buf, tx);
        TEST_ASSERT_TRUE_MESSAGE(len > 0, c.ctx);

        struct aprsMessage rx;
        initAPRS(rx, 0x00);
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(c.expect_rc, decodeAPRS(buf, len, rx), c.ctx);
    }
}

// Shortest legal frame this encoder produces: a 1-byte payload (an empty
// payload makes encodePayloadAPRS() return 0 and encodeAPRS() bails out with
// 0 -- that boundary is noted here, not exercised, since a zero-length
// payload is refused rather than truncated). src "DK5EN-90" (8) + dst "9999"
// (4) -> header 20 bytes, +1 payload +9 epilogue = 30 bytes total, comfortably
// clearing decodeAPRS()'s 16-byte minimum.
static void test_epilogue_kuerzester_frame(void)
{
    struct aprsMessage tx;
    buildMessage(tx, ':', 0x01UL, "X", 9, 0x88, 35, 0x80 | 9, 'p');

    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = encodeAPRS(buf, tx);
    TEST_ASSERT_EQUAL_UINT16(30, len);
    assertEpilogue(buf, len, 9, 0x88, (uint16_t)tx.msg_fcs, 35, 0x80 | 9, 'p', "shortest frame");

    struct aprsMessage rx;
    initAPRS(rx, 0x00);
    TEST_ASSERT_EQUAL_HEX16(0x3A, decodeAPRS(buf, len, rx));
    TEST_ASSERT_EQUAL_STRING("X", rx.msg_payload.c_str());

    // The empty-payload boundary: encodeAPRS() returns 0, it does not write
    // a frame with an empty payload and a bare epilogue.
    struct aprsMessage empty;
    buildMessage(empty, ':', 0x02UL, "", 9, 0x88, 35, 0x80 | 9, 'p');
    uint8_t buf2[UDP_TX_BUF_SIZE] = {0};
    TEST_ASSERT_EQUAL_UINT16(0, encodeAPRS(buf2, empty));
}

// Maximum length the encoder accepts before it truncates the payload to make
// room for the fixed-size epilogue. encodeAPRS() reserves 10 bytes once
// header+payload would leave fewer than that (aprs_functions.cpp:1323-1324),
// clamping to UDP_TX_BUF_SIZE-10 = 245; the 9-byte epilogue is then appended
// unconditionally, landing the encoder's ceiling at 245+9 = 254 -- one byte
// under UDP_TX_BUF_SIZE (255), which is why every write here stays in-bounds.
// header("DK5EN-90">"9999"+type) = 20 bytes, so a 235-byte payload
// (20+235=255) is deliberately chosen to be comfortably past the 245
// threshold -- large enough to prove real truncation (10 payload bytes are
// overwritten by the epilogue, not just an exact-fit coincidence) while every
// byte written (up to index 254) still fits inside the 255-byte buffer. NOT
// exercised: pushing further to also trip the second, redundant clamp at
// line 1365 (`inext > UDP_TX_BUF_SIZE`) -- given these constants the first
// clamp always leaves inext <= 254, so that second clamp looks unreachable
// and forcing it would mean fabricating a buffer larger than
// UDP_TX_BUF_SIZE, which is exactly the out-of-bounds write this test avoids.
static void test_epilogue_max_laenge_trunkierung(void)
{
    char payload[236];
    memset(payload, 'A', 235);
    payload[235] = '\0';

    struct aprsMessage tx;
    buildMessage(tx, ':', 0x03UL, payload, 9, 0x88, 35, 0x80 | 9, 'p');

    uint8_t buf[UDP_TX_BUF_SIZE] = {0};
    uint16_t len = encodeAPRS(buf, tx);

    TEST_ASSERT_EQUAL_UINT16(254, len);
    TEST_ASSERT_EQUAL_HEX8('A', buf[244]);      // last surviving payload byte
    assertEpilogue(buf, len, 9, 0x88, (uint16_t)tx.msg_fcs, 35, 0x80 | 9, 'p', "max length");

    struct aprsMessage rx;
    initAPRS(rx, 0x00);
    TEST_ASSERT_EQUAL_HEX16(0x3A, decodeAPRS(buf, len, rx));

    char expect_payload[226];
    memset(expect_payload, 'A', 225);
    expect_payload[225] = '\0';
    TEST_ASSERT_EQUAL_STRING(expect_payload, rx.msg_payload.c_str());
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_epilogue_bytegenau_alle_typen);
    RUN_TEST(test_epilogue_fcs_von_hand);
    RUN_TEST(test_epilogue_roundtrip_alle_typen);
    RUN_TEST(test_epilogue_sub_substitution);
    RUN_TEST(test_epilogue_sub_0x7e_collision);
    RUN_TEST(test_epilogue_fw_version_grenzen);
    RUN_TEST(test_epilogue_kuerzester_frame);
    RUN_TEST(test_epilogue_max_laenge_trunkierung);
    return UNITY_END();
}
