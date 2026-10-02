// Campaign docs/campaign-extudp-hwid.md: EXTUDP "msg" (text) datagram key
// contract (src/extern_msg_json.h). Three new keys -- hw_id, lora_mod,
// max_hop -- are appended after the pre-existing wire shape; test_golden_*
// and test_lora_keys_and_order pin the byte-for-byte order, and
// the two test_worst_case_* cases are the buffer budget check from
// docs/2026-09-16_firmware-extudp-hw-id-on-text-frames.md section 5 (the
// air-side bound is what sizes EXTERN_MSG_JSON_BUF).

#include <unity.h>
#include <string.h>
#include <ArduinoJson.h>
#include <extern_msg_json.h>

void setUp(void) {}
void tearDown(void) {}

// Key order + the lora_mod mask: mod_byte 0x38 = country nibble 3, modulation
// nibble 8 -- lora_mod must come out as 8, not 0x38.
static void test_lora_keys_and_order(void)
{
    char out[300];
    size_t len = externMsgJson(out, sizeof(out), "lora",
                               "OE1XXX-1", "9", "test", "ABCDEF12",
                               nullptr, 35, "t",
                               -80, -10,
                               43, 0x38, 4);
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());
    TEST_ASSERT_EQUAL_UINT(43, doc["hw_id"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT(8, doc["lora_mod"].as<unsigned>());
    TEST_ASSERT_EQUAL_UINT(4, doc["max_hop"].as<unsigned>());
    TEST_ASSERT_TRUE(doc["firmware"].is<int>());
    TEST_ASSERT_EQUAL_INT(35, doc["firmware"].as<int>());

    // Key order: byte-for-byte, not just key presence.
    static const char prefix[] = "{\"src_type\":\"lora\",\"type\":\"msg\",\"src\":";
    TEST_ASSERT_EQUAL_INT(0, strncmp(out, prefix, strlen(prefix)));

    static const char tail[] = "\"snr\":-10,\"hw_id\":43,\"lora_mod\":8,\"max_hop\":4}";
    size_t tail_len = strlen(tail);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(tail_len, len);
    TEST_ASSERT_EQUAL_INT(0, strcmp(out + len - tail_len, tail));
}

// firmware asymmetry: "node" carries the version STRING, never the int.
static void test_node_firmware_is_string(void)
{
    char out[300];
    size_t len = externMsgJson(out, sizeof(out), "node",
                               "DK5EN-1", "9", "hi", "01234567",
                               "4.35", 0, "t",
                               0, 0,
                               43, 0x03, 7);
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());
    TEST_ASSERT_TRUE(doc["firmware"].is<const char *>());
    TEST_ASSERT_EQUAL_STRING("4.35", doc["firmware"]);
    TEST_ASSERT_EQUAL_UINT(43, doc["hw_id"].as<unsigned>());
}

// Golden "before" datagram (docs/campaign-extudp-hwid.md), captured
// 2026-09-18 on DK5EN-93 before this campaign. The old wire must survive
// byte-for-byte as a PREFIX; the three new keys are appended, never spliced
// in, so an old-firmware capture and a new one agree on every byte the old
// one had.
static void test_golden_before_prefix(void)
{
    char out[300];
    size_t len = externMsgJson(out, sizeof(out), "node",
                               "DK5EN-1", "9", "golden before hw_id", "EA25A384",
                               "4.35", 0, "t",
                               0, 0,
                               43, 0x03, 7);
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    static const char golden_prefix[] =
        "{\"src_type\":\"node\",\"type\":\"msg\",\"src\":\"DK5EN-1\",\"dst\":\"9\","
        "\"msg\":\"golden before hw_id\",\"msg_id\":\"EA25A384\",\"firmware\":\"4.35\","
        "\"fw_sub\":\"t\",\"rssi\":0,\"snr\":0";
    size_t prefix_len = strlen(golden_prefix);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(prefix_len, len);
    TEST_ASSERT_EQUAL_INT(0, strncmp(out, golden_prefix, prefix_len));

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());
    TEST_ASSERT_EQUAL_STRING("golden before hw_id", doc["msg"]);
}

// Budget check (docs/2026-09-16_..., section 5): worst legal text frame
// under the pre-campaign firmware limits --
//   - src: a 5-hop relay path, ~54 chars
//     (DL1UDO-12,DO8RE-12,DO7GH-0,DB0HOB-12,DB0ED-99,DK5EN-99)
//   - dst: 9 chars (the getExtern() config-frame dst maximum)
//   - msg: 150 '"' characters -- the getExtern() config-frame msg maximum
//     (src/extudp_functions.cpp:246-249, "dst allowed up to 9 characters and
//     msg up to 150"; the web GUI textarea maxlength is 149, one less to
//     leave room for the destination call, web_functions.cpp:1596). Every
//     byte is a double quote so each one escapes to two bytes on the wire
//     (300 bytes for the "msg" value alone) -- the worst case ArduinoJson's
//     escaper can produce for a 150-character payload.
// Measured at the wave-1 gate: 530 bytes, over the former c_json[500] in
// sendExtern() (src/extudp_functions.cpp). This is the config-frame input
// bound; the AIR bound below is larger and sizes EXTERN_MSG_JSON_BUF. The assertion is against that constant so a
// later key addition that outgrows the buffer fails here, natively, instead
// of truncating the datagram on the node (JSN-01: serializeJson() stops at
// the buffer and the proxy then drops the unparseable frame).
static void test_worst_case_fits_buffer(void)
{
    char msg[151];
    memset(msg, '"', 150);
    msg[150] = 0;

    char out[EXTERN_MSG_JSON_BUF + 100];
    size_t len = externMsgJson(out, sizeof(out), "lora",
                               "DL1UDO-12,DO8RE-12,DO7GH-0,DB0HOB-12,DB0ED-99,DK5EN-99",
                               "123456789", msg, "EA25A384",
                               nullptr, 35, "t",
                               -121, -10,
                               255, 0xFF, 15);
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());
    TEST_ASSERT_EQUAL_STRING(msg, doc["msg"]);

    // Measured 530; the datagram must fit the node's buffer with margin
    // (serializeJson() needs the terminator too, hence strictly less).
    TEST_ASSERT_EQUAL_UINT(530, (unsigned)len);
    TEST_ASSERT_LESS_THAN_UINT(EXTERN_MSG_JSON_BUF, (unsigned)len);
}

// The air-side bound (advisor finding, wave-1 gate): encodeAPRS() caps
// "src>dst:payload" at 239 bytes (src/aprs_functions.cpp, UDP_TX_BUF_SIZE -
// 10 minus the 6-byte header), so a frame off the air -- from any firmware,
// or relayed by the server -- may carry far more than the 150-character
// config-frame input cap above. The JSON is longest for a SHORT src and dst
// and a payload of nothing but '"': 5 + 1 + 1 + 1 + 231 = 239, every numeric
// key at its widest. This is the case EXTERN_MSG_JSON_BUF is sized for.
static void test_worst_case_air_frame_fits_buffer(void)
{
    char msg[232];
    memset(msg, '"', 231);
    msg[231] = 0;

    char out[EXTERN_MSG_JSON_BUF + 100];
    size_t len = externMsgJson(out, sizeof(out), "lora",
                               "DK5EN", "*", msg, "EA25A384",
                               nullptr, 255, "t",
                               -121, -10,
                               255, 0xFF, 15);
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());
    TEST_ASSERT_EQUAL_STRING(msg, doc["msg"]);

    TEST_ASSERT_EQUAL_UINT(636, (unsigned)len);
    TEST_ASSERT_LESS_THAN_UINT(EXTERN_MSG_JSON_BUF, (unsigned)len);
}

static void test_null_buffer_returns_zero(void)
{
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)externMsgJson(nullptr, 0, "lora",
                                                       "X", "9", "m", "00000000",
                                                       nullptr, 35, "t",
                                                       0, 0, 0, 0, 0));
    char out[10];
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)externMsgJson(out, 0, "lora",
                                                       "X", "9", "m", "00000000",
                                                       nullptr, 35, "t",
                                                       0, 0, 0, 0, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_lora_keys_and_order);
    RUN_TEST(test_node_firmware_is_string);
    RUN_TEST(test_golden_before_prefix);
    RUN_TEST(test_worst_case_fits_buffer);
    RUN_TEST(test_worst_case_air_frame_fits_buffer);
    RUN_TEST(test_null_buffer_returns_zero);
    return UNITY_END();
}
