// FORMAL-01 DS-02 (IDF #2, GHSA-g764-gwc3-75m5): host test of the bounded DHCP option walker in
// src/esp32/idf_override/dhcpserver.c. MESHCOM_DHCPS_HOST_TEST compiles only dhcps_opt_step().
#include <unity.h>
#include <stdint.h>

#define MESHCOM_DHCPS_HOST_TEST 1
#include "esp32/idf_override/dhcpserver.c"

void setUp(void) {}
void tearDown(void) {}

static void test_option_ending_exactly_at_avail_is_read(void)
{
    const uint8_t o[] = {53, 1, 3};   // message type REQUEST, ends at avail
    uint8_t code = 0, len = 0;
    uint16_t next = 0;
    TEST_ASSERT_EQUAL_INT(1, dhcps_opt_step(o, sizeof(o), 0, &code, &len, &next));
    TEST_ASSERT_EQUAL_UINT8(53, code);
    TEST_ASSERT_EQUAL_UINT8(1, len);
    TEST_ASSERT_EQUAL_UINT16(3, next);
    TEST_ASSERT_EQUAL_INT(0, dhcps_opt_step(o, sizeof(o), next, &code, &len, &next));
}

static void test_value_one_byte_too_long_is_refused(void)
{
    const uint8_t o[] = {50, 4, 192, 168, 4};   // requested IP claims 4 bytes, 3 present
    uint8_t code = 0, len = 0;
    uint16_t next = 0;
    TEST_ASSERT_EQUAL_INT(0, dhcps_opt_step(o, sizeof(o), 0, &code, &len, &next));
}

static void test_length_byte_missing_is_refused(void)
{
    const uint8_t o[] = {1, 4, 255, 255, 255, 0, 51};   // option 51 at the last byte, no length byte
    uint8_t code = 0, len = 0;
    uint16_t next = 0;
    TEST_ASSERT_EQUAL_INT(1, dhcps_opt_step(o, sizeof(o), 0, &code, &len, &next));
    TEST_ASSERT_EQUAL_UINT16(6, next);
    TEST_ASSERT_EQUAL_INT(0, dhcps_opt_step(o, sizeof(o), next, &code, &len, &next));
}

static void test_pad_and_end_are_one_byte(void)
{
    const uint8_t o[] = {0, 0, 255};
    uint8_t code = 9, len = 9;
    uint16_t next = 0;
    TEST_ASSERT_EQUAL_INT(1, dhcps_opt_step(o, sizeof(o), 0, &code, &len, &next));
    TEST_ASSERT_EQUAL_UINT8(0, code);
    TEST_ASSERT_EQUAL_UINT16(1, next);
    TEST_ASSERT_EQUAL_INT(1, dhcps_opt_step(o, sizeof(o), 2, &code, &len, &next));
    TEST_ASSERT_EQUAL_UINT8(255, code);
    TEST_ASSERT_EQUAL_UINT8(0, len);
}

static void test_pos_at_or_after_end_stops(void)
{
    const uint8_t o[] = {53, 1, 1};
    uint8_t code = 0, len = 0;
    uint16_t next = 0;
    TEST_ASSERT_EQUAL_INT(0, dhcps_opt_step(o, sizeof(o), 3, &code, &len, &next));
    TEST_ASSERT_EQUAL_INT(0, dhcps_opt_step(o, sizeof(o), 200, &code, &len, &next));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_option_ending_exactly_at_avail_is_read);
    RUN_TEST(test_value_one_byte_too_long_is_refused);
    RUN_TEST(test_length_byte_missing_is_refused);
    RUN_TEST(test_pad_and_end_are_one_byte);
    RUN_TEST(test_pos_at_or_after_end_stops);
    return UNITY_END();
}
