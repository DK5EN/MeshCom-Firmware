// TLM-04: EXTUDP "tele" datagram key contract (src/extern_tele_json.h).
//
// Field sample 2026-09-04: gateway "Primaer" relayed DM3KS-13 (BME680) with
// "qfe":191 -- the /F= pressure altitude in metres, not the /P= station
// pressure. The lora test below fails on the pre-fix mapping (qfe <- /F=)
// and passes once qfe carries /P=.

#include <unity.h>
#include <string.h>
#include <ArduinoJson.h>
#include <extern_tele_json.h>

void setUp(void) {}
void tearDown(void) {}

// Relayed node: /P=990.5 /F=191 /Q=0 (BME680 suppresses /Q=).
static void test_lora_qfe_is_station_pressure_not_altitude(void)
{
    char out[300];
    size_t len = externTeleJsonLora(out, sizeof(out), "DM3KS-13", 100,
                                    21.5f, 0.0f, 55.0f,
                                    990.5f, 0.0f, 191,
                                    0.0f, 0.0f);
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());
    TEST_ASSERT_EQUAL_STRING("lora", doc["src_type"]);
    TEST_ASSERT_EQUAL_STRING("tele", doc["type"]);
    TEST_ASSERT_EQUAL_STRING("DM3KS-13", doc["src"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 990.5f, doc["qfe"].as<float>());
    TEST_ASSERT_EQUAL_INT(191, doc["pressure_alt"].as<int>());
    TEST_ASSERT_EQUAL_INT(100, doc["batt"].as<int>());
}

// Own sensor: qfe/qnh were already hPa here; must stay so.
static void test_node_qfe_qnh_are_pressures(void)
{
    char out[300];
    size_t len = externTeleJsonNode(out, sizeof(out), "DK5EN-98",
                                    23.3f, 0.0f, 60.0f,
                                    1018.5f, 1044.2f,
                                    0.0f, 0.0f);
    TEST_ASSERT_GREATER_THAN_UINT(0, (unsigned)len);

    JsonDocument doc;
    TEST_ASSERT_EQUAL(DeserializationError::Ok, deserializeJson(doc, out).code());
    TEST_ASSERT_EQUAL_STRING("node", doc["src_type"]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1018.5f, doc["qfe"].as<float>());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1044.2f, doc["qnh"].as<float>());
    TEST_ASSERT_FALSE(doc.containsKey("pressure_alt"));
}

// JSN-01: bound by the buffer -- canary bytes behind it stay untouched.
static void test_small_buffer_is_bounded(void)
{
    char raw[32 + 8];
    memset(raw, 0xAA, sizeof(raw));
    size_t len = externTeleJsonLora(raw, 32, "DM3KS-13", 100,
                                    21.5f, 0.0f, 55.0f, 990.5f, 0.0f, 191, 0.0f, 0.0f);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(32, (unsigned)len);
    for(int i = 0; i < 8; i++)
        TEST_ASSERT_EQUAL_HEX8(0xAA, (uint8_t)raw[32 + i]);
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)externTeleJsonLora(nullptr, 0, "X", 0,
                                                           0, 0, 0, 0, 0, 0, 0, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_lora_qfe_is_station_pressure_not_altitude);
    RUN_TEST(test_node_qfe_qnh_are_pressures);
    RUN_TEST(test_small_buffer_is_bounded);
    return UNITY_END();
}
