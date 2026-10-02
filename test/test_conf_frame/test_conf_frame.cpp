// TM-39: server-pushed CONF provisioning -- parser tests for
// src/conf_frame.cpp/.h (the ESP32 counterpart of the nRF52 CONF handler,
// src/nrf52/nrf_eth.cpp:660-768). parseConfFrame() is a pure function (no
// Arduino dependency), so this suite links only that one file and compiles
// entirely on the host -- no stubs needed.
//
//   pio test -e native_conf_frame   (scratch env; see orchestrator notes --
//   this suite is not yet registered in the project platformio.ini)
//
// N-03 (nrf_eth.cpp) is the reason every case below that carries a lying
// <len> byte is deliberately checked for "rejected, not a crash": that bug
// let a spoofed length byte drive a write far past a 255-byte stack buffer.
// parseConfFrame() bounds every length/offset read against the actual
// buffer length (`len`) before touching it, and this suite is the proof.
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "conf_frame.h"

// ------------------------------------------------------------- Frame builder
//
// Small append-only byte buffer mirroring how a server would assemble a CONF
// payload (the part AFTER the 4-byte "CONF" indicator -- parseConfFrame()
// never sees the indicator itself, matching how src/udp_functions.cpp calls
// it: inc_udp_buffer + UDP_MSG_INDICATOR_LEN).
struct FrameBuilder
{
    uint8_t buf[300];
    int     len = 0;

    void byte(uint8_t b) { buf[len++] = b; }

    void bytes(const uint8_t *p, int n)
    {
        memcpy(buf + len, p, (size_t)n);
        len += n;
    }

    // TLV with an honest length byte (== the number of data bytes appended)
    void tlv(uint8_t tag, const char *data)
    {
        int n = (int)strlen(data);
        byte(tag);
        byte((uint8_t)n);
        bytes((const uint8_t *)data, n);
    }

    // TLV whose length byte LIES: declares `claimedLen` bytes but only
    // `actualLen` are actually appended -- the shape of a hostile or
    // corrupted datagram (this is exactly what N-03 in nrf_eth.cpp got
    // wrong: it trusted the declared length).
    void lyingTlv(uint8_t tag, uint8_t claimedLen, const char *data, int actualLen)
    {
        byte(tag);
        byte(claimedLen);
        bytes((const uint8_t *)data, actualLen);
    }

    void coord(uint8_t tag, int32_t v)
    {
        byte(tag);
        // same byte order parseConfFrame()/nrf_eth.cpp assemble: byte 0 is
        // the least-significant byte of the 32-bit value.
        uint32_t u = (uint32_t)v;
        byte((uint8_t)(u & 0xFF));
        byte((uint8_t)((u >> 8) & 0xFF));
        byte((uint8_t)((u >> 16) & 0xFF));
        byte((uint8_t)((u >> 24) & 0xFF));
    }
};

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- Test cases

// Full valid frame: call, shortname, lat, lon, alt -- every field must come
// back exactly as sent, and hasX must be true for all five.
static void test_full_frame_alle_felder(void)
{
    FrameBuilder f;
    f.tlv(0x00, "OE1XYZ-10");
    f.tlv(0x01, "XYZ10");
    f.coord(0x02, 481234567);
    f.coord(0x03, 162345678);
    f.coord(0x04, -1500);

    ConfFrame out;
    TEST_ASSERT_TRUE(parseConfFrame(f.buf, f.len, out));

    TEST_ASSERT_TRUE(out.hasCall);
    TEST_ASSERT_EQUAL_STRING("OE1XYZ-10", out.call);
    TEST_ASSERT_TRUE(out.hasShort);
    TEST_ASSERT_EQUAL_STRING("XYZ10", out.shortname);
    TEST_ASSERT_TRUE(out.hasLat);
    TEST_ASSERT_EQUAL_INT32(481234567, out.lat);
    TEST_ASSERT_TRUE(out.hasLon);
    TEST_ASSERT_EQUAL_INT32(162345678, out.lon);
    TEST_ASSERT_TRUE(out.hasAlt);
    TEST_ASSERT_EQUAL_INT32(-1500, out.alt);
}

// Call-only frame (no 0x01/0x02/0x03/0x04 tags at all) -- the shortname and
// coordinate fields are genuinely optional on the wire.
static void test_call_only_frame(void)
{
    FrameBuilder f;
    f.tlv(0x00, "OE5ABC");

    ConfFrame out;
    TEST_ASSERT_TRUE(parseConfFrame(f.buf, f.len, out));

    TEST_ASSERT_TRUE(out.hasCall);
    TEST_ASSERT_EQUAL_STRING("OE5ABC", out.call);
    TEST_ASSERT_FALSE(out.hasShort);
    TEST_ASSERT_FALSE(out.hasLat);
    TEST_ASSERT_FALSE(out.hasLon);
    TEST_ASSERT_FALSE(out.hasAlt);
}

// Call TLV's own length byte lies: declares 40 bytes of callsign but only 6
// are actually in the buffer. Must be rejected outright, and must never read
// past `len` doing it (ASan/valgrind would catch that; the assertion here is
// the observable half: parseConfFrame() returns false and out.hasCall is
// never set true on the strength of an unread tail).
static void test_call_laenge_luegt_ueber_puffer_hinaus(void)
{
    FrameBuilder f;
    f.lyingTlv(0x00, 40, "OE1ABC", 6);   // claims 40, buffer only has 6

    ConfFrame out;
    TEST_ASSERT_FALSE(parseConfFrame(f.buf, f.len, out));
}

// Call parses fine, but the trailing shortname TLV's length byte lies past
// the buffer -- the whole frame is rejected (not just the shortname), so a
// malformed tail cannot smuggle a callsign change past the caller.
static void test_shortname_laenge_luegt_verwirft_gesamten_frame(void)
{
    FrameBuilder f;
    f.tlv(0x00, "OE1ABC");
    f.lyingTlv(0x01, 100, "XY", 2);   // claims 100, buffer only has 2 more

    ConfFrame out;
    TEST_ASSERT_FALSE(parseConfFrame(f.buf, f.len, out));
}

// A coordinate tag present but with fewer than 4 bytes left in the buffer
// (truncated datagram) must also be rejected, not read past the end.
static void test_koordinate_abgeschnitten(void)
{
    FrameBuilder f;
    f.tlv(0x00, "OE1ABC");
    f.byte(0x02);
    f.byte(0x11);
    f.byte(0x22);   // only 2 of the required 4 coordinate bytes present

    ConfFrame out;
    TEST_ASSERT_FALSE(parseConfFrame(f.buf, f.len, out));
}

// len == 0 (e.g. packetSize was exactly UDP_MSG_INDICATOR_LEN -- "CONF" and
// nothing else): nothing to parse, must be rejected without touching
// payload at all.
static void test_leerer_payload(void)
{
    uint8_t dummy = 0;
    ConfFrame out;
    TEST_ASSERT_FALSE(parseConfFrame(&dummy, 0, out));
}

// A null payload pointer paired with len <= 0 must also be handled, not
// dereferenced -- defends the caller against ever passing a zero-length
// buffer's address-of-nothing.
static void test_null_payload(void)
{
    ConfFrame out;
    TEST_ASSERT_FALSE(parseConfFrame(nullptr, 0, out));
}

// Wire callsign length byte is 0x00 -- an explicit empty callsign. Rejected:
// an empty callsign is never something the caller should apply.
static void test_null_laenge_callsign(void)
{
    FrameBuilder f;
    f.byte(0x00);
    f.byte(0x00);   // call length 0, no call bytes follow

    ConfFrame out;
    TEST_ASSERT_FALSE(parseConfFrame(f.buf, f.len, out));
}

// Shortname longer than CONF_FRAME_SHORT_MAX (5, matching
// meshcom_settings.node_short[6]) is TRUNCATED, not rejected -- the
// callsign (the field that actually changes node identity) already passed
// the mandatory-tag check by this point, and the shortname is a cosmetic
// display field the caller re-derives from the callsign anyway when it is
// absent. Failing the whole provisioning push over an overlong shortname
// would be a worse outcome than a clipped display name.
static void test_shortname_zu_lang_wird_gekuerzt(void)
{
    FrameBuilder f;
    f.tlv(0x00, "OE1ABC");
    f.tlv(0x01, "TOOLONGNAME");   // 11 chars, cap is 5

    ConfFrame out;
    TEST_ASSERT_TRUE(parseConfFrame(f.buf, f.len, out));
    TEST_ASSERT_TRUE(out.hasShort);
    TEST_ASSERT_EQUAL_STRING("TOOLO", out.shortname);   // first 5 chars
    TEST_ASSERT_EQUAL_INT(5, (int)strlen(out.shortname));
}

// Callsign longer than CONF_FRAME_CALL_MAX (9, matching
// meshcom_settings.node_call[10]) is truncated the same way, and parsing
// still succeeds and continues past it to later tags.
static void test_callsign_zu_lang_wird_gekuerzt(void)
{
    FrameBuilder f;
    f.tlv(0x00, "OE1ABCDEFGH-99");   // 14 chars, cap is 9
    f.tlv(0x01, "ABCDE");

    ConfFrame out;
    TEST_ASSERT_TRUE(parseConfFrame(f.buf, f.len, out));
    TEST_ASSERT_EQUAL_STRING("OE1ABCDEF", out.call);   // first 9 chars
    TEST_ASSERT_EQUAL_INT(9, (int)strlen(out.call));
    TEST_ASSERT_TRUE(out.hasShort);
    TEST_ASSERT_EQUAL_STRING("ABCDE", out.shortname);
}

// packetSize right at the UDP_CONF_BUFF_SIZE (255) boundary: the caller
// passes len = packetSize - UDP_MSG_INDICATOR_LEN, so up to 251 bytes here.
// A full, honestly-labelled frame padded out to exactly that size must
// parse cleanly and not overrun anything.
static void test_puffergrenze_251_bytes(void)
{
    FrameBuilder f;
    f.tlv(0x00, "OE1ABC-1");    // 8 chars (fits the 9-char cap)
    f.tlv(0x01, "ABC1");        // 4 chars (fits the 5-char cap)
    f.coord(0x02, 1);
    f.coord(0x03, 2);
    f.coord(0x04, 3);
    // pad out to exactly 251 bytes with a trailing unknown tag byte that
    // parseConfFrame() simply stops at (no tag it recognizes follows 0x04's
    // 4 data bytes, so the extra bytes are just unread trailer -- parsing
    // 251 real bytes total is the point of this test, not this padding tag)
    while(f.len < 251)
        f.byte(0xFF);
    TEST_ASSERT_EQUAL_INT(251, f.len);

    ConfFrame out;
    TEST_ASSERT_TRUE(parseConfFrame(f.buf, f.len, out));
    TEST_ASSERT_EQUAL_STRING("OE1ABC-1", out.call);
    TEST_ASSERT_EQUAL_STRING("ABC1", out.shortname);
    TEST_ASSERT_EQUAL_INT32(1, out.lat);
    TEST_ASSERT_EQUAL_INT32(2, out.lon);
    TEST_ASSERT_EQUAL_INT32(3, out.alt);
}

// Missing mandatory 0x00 tag at all (frame starts with something else) --
// mirrors the nRF52 handler's "config_buf[0] != 0x00" discard branch.
static void test_fehlender_pflicht_tag(void)
{
    FrameBuilder f;
    f.tlv(0x01, "SHORT");   // shortname first, no callsign tag at all

    ConfFrame out;
    TEST_ASSERT_FALSE(parseConfFrame(f.buf, f.len, out));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_frame_alle_felder);
    RUN_TEST(test_call_only_frame);
    RUN_TEST(test_call_laenge_luegt_ueber_puffer_hinaus);
    RUN_TEST(test_shortname_laenge_luegt_verwirft_gesamten_frame);
    RUN_TEST(test_koordinate_abgeschnitten);
    RUN_TEST(test_leerer_payload);
    RUN_TEST(test_null_payload);
    RUN_TEST(test_null_laenge_callsign);
    RUN_TEST(test_shortname_zu_lang_wird_gekuerzt);
    RUN_TEST(test_callsign_zu_lang_wird_gekuerzt);
    RUN_TEST(test_puffergrenze_251_bytes);
    RUN_TEST(test_fehlender_pflicht_tag);
    return UNITY_END();
}
