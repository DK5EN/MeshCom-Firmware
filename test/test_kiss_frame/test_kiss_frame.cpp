// Host unit tests for the R2-04 char[]-port of upstream's KISS AX.25 build /
// ack-rewrite (src/kiss_frame.h -- carved out of src/kiss_functions.cpp so
// the port off Arduino String has an executable test). No sockets, no
// Arduino runtime beyond the test/support stub aprs_structures.h needs. Run
// with:
//   pio test -e native_kiss_frame
//
// Expected bytes for the AX.25 vectors are derived from ax25EncodeAddr()
// (lib/kiss_ax25/kiss_ax25.cpp) applied to the same calls kissBuildAx25()
// itself is given -- i.e. the encoder is exercised twice, once inside the
// function under test and once here to build the expected header, and the
// two are compared byte-for-byte. That catches a wrong field order, a wrong
// topbit/last-bit, or a wrong info-field format that a length-only check
// would miss.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "kiss_frame.h"
#include "configuration_global.h"   // MSG_TYPE_TEXT / MSG_TYPE_POSITION

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static void clearMsg(struct aprsMessage &m)
{
    memset(&m, 0, sizeof(m));
}

// ===========================================================================
// kissAckRewrite
// ===========================================================================

void test_ackrewrite_ack_form_substitutes_client_nn(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, ":ack123");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[2].msg_id = (7u << 10) | 123u;   // low 10 bits = the on-air ack number
    strcpy(map[2].dst, "DK5EN-7");
    strcpy(map[2].nn, "42");

    uint32_t nodeNn = 0;
    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, &nodeNn);

    TEST_ASSERT_EQUAL_INT(2, idx);
    TEST_ASSERT_EQUAL_UINT32(123, nodeNn);
    TEST_ASSERT_EQUAL_STRING(":ack42", payload);
    // map itself is untouched -- consuming the slot is the caller's job
    TEST_ASSERT_NOT_EQUAL(0, map[2].msg_id);
}

void test_ackrewrite_rej_form_substitutes_client_nn(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, ":rej99");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[0].msg_id = 99u;                 // low 10 bits = 99, high bits 0
    strcpy(map[0].dst, "DK5EN-1");
    strcpy(map[0].nn, "7");

    uint32_t nodeNn = 0;
    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-1", map, &nodeNn);

    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_UINT32(99, nodeNn);
    TEST_ASSERT_EQUAL_STRING(":rej7", payload);
}

void test_ackrewrite_no_matching_entry_leaves_payload_unchanged(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, ":ack123");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));         // every slot free (msg_id == 0)

    uint32_t nodeNn = 999;
    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, &nodeNn);

    TEST_ASSERT_EQUAL_INT(-1, idx);
    TEST_ASSERT_EQUAL_STRING(":ack123", payload);
    TEST_ASSERT_EQUAL_UINT32(999, nodeNn);   // untouched on no-match
}

void test_ackrewrite_wrong_dst_no_match(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, ":ack123");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[0].msg_id = 123u;
    strcpy(map[0].dst, "DK5EN-7");
    strcpy(map[0].nn, "42");

    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-98", map, nullptr);

    TEST_ASSERT_EQUAL_INT(-1, idx);
    TEST_ASSERT_EQUAL_STRING(":ack123", payload);
}

void test_ackrewrite_no_digits_after_ack_leaves_payload_unchanged(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, ":ackXYZ");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[0].msg_id = 1u;
    strcpy(map[0].dst, "DK5EN-7");
    strcpy(map[0].nn, "1");

    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, nullptr);

    TEST_ASSERT_EQUAL_INT(-1, idx);
    TEST_ASSERT_EQUAL_STRING(":ackXYZ", payload);
}

void test_ackrewrite_no_ack_or_rej_token_leaves_payload_unchanged(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, "just some text, no ack here");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));

    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, nullptr);

    TEST_ASSERT_EQUAL_INT(-1, idx);
    TEST_ASSERT_EQUAL_STRING("just some text, no ack here", payload);
}

// The real MeshCom shape: a 9-char padded addressee before ":ack", and text
// after the digits. Prefix and tail must survive the splice unchanged, and a
// shorter "{nn" than the digits it replaces must pull the tail left (memmove
// length and destination both matter here, unlike in the bare ":ack123").
void test_ackrewrite_real_shape_keeps_prefix_and_tail(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, "DK5EN-7  :ack123}xyz");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[5].msg_id = (3u << 10) | 123u;
    strcpy(map[5].dst, "DK5EN-7");
    strcpy(map[5].nn, "4");

    uint32_t nodeNn = 0;
    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, &nodeNn);

    TEST_ASSERT_EQUAL_INT(5, idx);
    TEST_ASSERT_EQUAL_UINT32(123, nodeNn);
    TEST_ASSERT_EQUAL_STRING("DK5EN-7  :ack4}xyz", payload);
}

// A longer "{nn" than the digits it replaces must push the tail right.
void test_ackrewrite_longer_nn_pushes_tail_right(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, "DK5EN-7  :ack7 tail");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[0].msg_id = 7u;
    strcpy(map[0].dst, "DK5EN-7");
    strcpy(map[0].nn, "AB12");

    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, nullptr);

    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_STRING("DK5EN-7  :ackAB12 tail", payload);
}

// Upstream searches ":ack" first and only then ":rej" (first occurrence of
// each). With both present, the ":ack" number decides even when ":rej" comes
// earlier in the payload: here only slot 123 exists, so a port that searched
// ":rej" first would parse 999, find no slot and leave the payload untouched.
void test_ackrewrite_ack_is_searched_before_rej(void)
{
    char payload[MC_PAYLOAD_LEN];
    strcpy(payload, "DK5EN-7  :rej999:ack123");

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[2].msg_id = 123u;
    strcpy(map[2].dst, "DK5EN-7");
    strcpy(map[2].nn, "42");

    uint32_t nodeNn = 0;
    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, &nodeNn);

    TEST_ASSERT_EQUAL_INT(2, idx);
    TEST_ASSERT_EQUAL_UINT32(123, nodeNn);
    TEST_ASSERT_EQUAL_STRING("DK5EN-7  :rej999:ack42", payload);
}

// Upstream built the replacement with String concatenation (prefix + nn +
// tail), which can grow without bound; msg_payload is the fixed
// MC_PAYLOAD_LEN buffer, so the port must clamp instead of overflow. This
// vector fills the payload right up to MC_PAYLOAD_LEN - 1 and gives the
// matching map slot a "{nn" (2 chars) that does NOT fit in the one byte of
// room left after the digits it replaces -- the splice must clamp the
// replacement to what fits and still NUL-terminate inside the buffer.
void test_ackrewrite_near_payload_len_clamps_without_overflow(void)
{
    char payload[MC_PAYLOAD_LEN];
    memset(payload, 'A', sizeof(payload));
    payload[MC_PAYLOAD_LEN - 1] = 0;          // full 255-char string, NUL at [255]

    // Overwrite the tail (starting at index 250) with ":ack1" -> total length
    // 255 = MC_PAYLOAD_LEN - 1, i.e. the buffer is exactly full.
    memcpy(payload + 250, ":ack1", 5);
    payload[255] = 0;
    TEST_ASSERT_EQUAL_size_t(255, strlen(payload));

    KissAckEntry map[KISS_ACKMAP_SLOTS];
    memset(map, 0, sizeof(map));
    map[1].msg_id = 1u;
    strcpy(map[1].dst, "DK5EN-7");
    strcpy(map[1].nn, "99");                  // 2 chars -- only 1 byte of room exists

    uint32_t nodeNn = 0;
    int idx = kissAckRewrite(payload, sizeof(payload), "DK5EN-7", map, &nodeNn);

    TEST_ASSERT_EQUAL_INT(1, idx);
    TEST_ASSERT_EQUAL_UINT32(1, nodeNn);

    // Expected: the 250 'A's, ":ack", then only "9" (nn clamped from "99" to
    // its first char -- the one byte of room), NUL-terminated, never past
    // index 255 of a 256-byte buffer.
    char expected[MC_PAYLOAD_LEN];
    memset(expected, 'A', 250);
    memcpy(expected + 250, ":ack9", 5);
    expected[255] = 0;

    TEST_ASSERT_EQUAL_STRING(expected, payload);
    TEST_ASSERT_EQUAL_size_t(255, strlen(payload));
    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)payload[255]);   // NUL inside the buffer
}

// ===========================================================================
// kissBuildAx25
// ===========================================================================

void test_buildax25_text_dm_no_digipeaters(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = MSG_TYPE_TEXT;
    strcpy(m.msg_source_call, "DK5EN-1");
    strcpy(m.msg_source_path, "DK5EN-1");        // origin only, no relays
    strcpy(m.msg_destination_call, "DK5EN-2");
    strcpy(m.msg_payload, "hello world");

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    uint8_t exp[128];
    size_t  eo = 0;
    eo += ax25EncodeAddr(exp + eo, "APRSMC", 0x80, false);     // destination
    eo += ax25EncodeAddr(exp + eo, "DK5EN-1", 0x00, true);     // source, last (no digis)
    exp[eo++] = 0x03;
    exp[eo++] = 0xF0;
    const char *info = ":DK5EN-2  :hello world";               // 9-char addressee, padded
    memcpy(exp + eo, info, strlen(info));
    eo += strlen(info);

    TEST_ASSERT_EQUAL_size_t(eo, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, eo);
}

void test_buildax25_text_two_hop_digipeaters(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = MSG_TYPE_TEXT;
    strcpy(m.msg_source_call, "DK5EN-1");
    strcpy(m.msg_source_path, "DK5EN-1,WIDE1-1,WIDE2-2");
    strcpy(m.msg_destination_call, "DK5EN-2");
    strcpy(m.msg_payload, "hi");

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    uint8_t exp[128];
    size_t  eo = 0;
    eo += ax25EncodeAddr(exp + eo, "APRSMC", 0x80, false);
    eo += ax25EncodeAddr(exp + eo, "DK5EN-1", 0x00, false);    // source, NOT last: 2 digis follow
    eo += ax25EncodeAddr(exp + eo, "WIDE1-1", 0x80, false);    // digi 1 of 2
    eo += ax25EncodeAddr(exp + eo, "WIDE2-2", 0x80, true);     // digi 2 of 2 -- last address bit
    exp[eo++] = 0x03;
    exp[eo++] = 0xF0;
    const char *info = ":DK5EN-2  :hi";
    memcpy(exp + eo, info, strlen(info));
    eo += strlen(info);

    TEST_ASSERT_EQUAL_size_t(eo, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, eo);

    // load-bearing bit checks, spelled out (not just byte-diff): the last
    // digipeater address must carry the HDLC extension bit, the one before
    // it must not.
    TEST_ASSERT_FALSE(out[14 + 6] & 0x01);   // WIDE1-1 address byte 7 (offset 14)
    TEST_ASSERT_TRUE(out[21 + 6] & 0x01);    // WIDE2-2 address byte 7 (offset 21)
}

void test_buildax25_star_relay_entries_skipped(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = MSG_TYPE_TEXT;
    strcpy(m.msg_source_call, "DK5EN-1");
    strcpy(m.msg_source_path, "DK5EN-1,*,WIDE2-1");    // "*" token dropped
    strcpy(m.msg_destination_call, "DK5EN-2");
    strcpy(m.msg_payload, "hi");

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    uint8_t exp[128];
    size_t  eo = 0;
    eo += ax25EncodeAddr(exp + eo, "APRSMC", 0x80, false);
    eo += ax25EncodeAddr(exp + eo, "DK5EN-1", 0x00, false);    // one digi follows
    eo += ax25EncodeAddr(exp + eo, "WIDE2-1", 0x80, true);     // the only (last) digi
    exp[eo++] = 0x03;
    exp[eo++] = 0xF0;
    const char *info = ":DK5EN-2  :hi";
    memcpy(exp + eo, info, strlen(info));
    eo += strlen(info);

    TEST_ASSERT_EQUAL_size_t(eo, n);   // 3 addresses (21 B), not 4 (28 B) -- "*" not encoded
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, eo);
}

void test_buildax25_preformatted_ack_payload_used_verbatim(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = MSG_TYPE_TEXT;
    strcpy(m.msg_source_call, "DK5EN-1");
    strcpy(m.msg_source_path, "DK5EN-1");
    strcpy(m.msg_destination_call, "DK5EN-2");
    strcpy(m.msg_payload, "DK5EN-2  :ack123");   // byte 9 == ':' -- already 9-padded

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    uint8_t exp[128];
    size_t  eo = 0;
    eo += ax25EncodeAddr(exp + eo, "APRSMC", 0x80, false);
    eo += ax25EncodeAddr(exp + eo, "DK5EN-1", 0x00, true);
    exp[eo++] = 0x03;
    exp[eo++] = 0xF0;
    const char *info = ":DK5EN-2  :ack123";       // ":" + payload verbatim, NOT re-padded
    memcpy(exp + eo, info, strlen(info));
    eo += strlen(info);

    TEST_ASSERT_EQUAL_size_t(eo, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, eo);
}

void test_buildax25_position_frame(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = MSG_TYPE_POSITION;
    strcpy(m.msg_source_call, "DK5EN-9");
    strcpy(m.msg_source_path, "DK5EN-9");
    strcpy(m.msg_payload, "4700.00N/01300.00Ez test");

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, sizeof(out));
    TEST_ASSERT_GREATER_THAN_size_t(0, n);

    uint8_t exp[128];
    size_t  eo = 0;
    eo += ax25EncodeAddr(exp + eo, "APRSMC", 0x80, false);
    eo += ax25EncodeAddr(exp + eo, "DK5EN-9", 0x00, true);
    exp[eo++] = 0x03;
    exp[eo++] = 0xF0;
    const char *info = "!4700.00N/01300.00Ez test";   // MSG_TYPE_POSITION == '!' (0x21)
    memcpy(exp + eo, info, strlen(info));
    eo += strlen(info);

    TEST_ASSERT_EQUAL_size_t(eo, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(exp, out, eo);
}

void test_buildax25_unknown_type_returns_zero(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = 0x3E;   // '>' -- HEY, not represented in v1
    strcpy(m.msg_source_call, "DK5EN-1");
    strcpy(m.msg_source_path, "DK5EN-1");
    strcpy(m.msg_payload, "some status text");

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_buildax25_short_source_call_returns_zero(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = MSG_TYPE_TEXT;
    strcpy(m.msg_source_call, "AB");   // < 3 chars
    strcpy(m.msg_source_path, "AB");
    strcpy(m.msg_destination_call, "DK5EN-2");
    strcpy(m.msg_payload, "hi");

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_buildax25_outsz_too_small_returns_zero(void)
{
    struct aprsMessage m;
    clearMsg(m);
    m.payload_type = MSG_TYPE_TEXT;
    strcpy(m.msg_source_call, "DK5EN-1");
    strcpy(m.msg_source_path, "DK5EN-1");
    strcpy(m.msg_destination_call, "DK5EN-2");
    strcpy(m.msg_payload, "hello world");

    uint8_t out[128];
    size_t n = kissBuildAx25(m, "APRSMC", out, 5);   // far below the ~38 B needed
    TEST_ASSERT_EQUAL_size_t(0, n);
}

// ---------------------------------------------------------------------------
// runner
// ---------------------------------------------------------------------------
void setUp(void) {}
void tearDown(void) {}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_ackrewrite_ack_form_substitutes_client_nn);
    RUN_TEST(test_ackrewrite_rej_form_substitutes_client_nn);
    RUN_TEST(test_ackrewrite_no_matching_entry_leaves_payload_unchanged);
    RUN_TEST(test_ackrewrite_wrong_dst_no_match);
    RUN_TEST(test_ackrewrite_no_digits_after_ack_leaves_payload_unchanged);
    RUN_TEST(test_ackrewrite_no_ack_or_rej_token_leaves_payload_unchanged);
    RUN_TEST(test_ackrewrite_real_shape_keeps_prefix_and_tail);
    RUN_TEST(test_ackrewrite_longer_nn_pushes_tail_right);
    RUN_TEST(test_ackrewrite_ack_is_searched_before_rej);
    RUN_TEST(test_ackrewrite_near_payload_len_clamps_without_overflow);

    RUN_TEST(test_buildax25_text_dm_no_digipeaters);
    RUN_TEST(test_buildax25_text_two_hop_digipeaters);
    RUN_TEST(test_buildax25_star_relay_entries_skipped);
    RUN_TEST(test_buildax25_preformatted_ack_payload_used_verbatim);
    RUN_TEST(test_buildax25_position_frame);
    RUN_TEST(test_buildax25_unknown_type_returns_zero);
    RUN_TEST(test_buildax25_short_source_call_returns_zero);
    RUN_TEST(test_buildax25_outsz_too_small_returns_zero);

    return UNITY_END();
}
