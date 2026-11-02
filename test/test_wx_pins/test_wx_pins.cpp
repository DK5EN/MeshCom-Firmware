// WX-01 (upstream #999): --windpin validation (src/wx_pins.h).
#include <unity.h>
#include "wx_pins.h"

void setUp(void) {}
void tearDown(void) {}

static const int R[] = {5, 4, 15, 13, 12, 0, -1};
#define NR (sizeof(R) / sizeof(R[0]))

static void test_zero_is_off_and_always_allowed(void)
{
    TEST_ASSERT_TRUE(wxPinAllowed(0, R, NR, false, false));
    TEST_ASSERT_TRUE(wxPinAllowed(0, R, NR, true, false));
}

static void test_reserved_board_pins_rejected(void)
{
    TEST_ASSERT_FALSE(wxPinAllowed(4, R, NR, false, false));
    TEST_ASSERT_FALSE(wxPinAllowed(15, R, NR, false, false));
    TEST_ASSERT_FALSE(wxPinAllowed(13, R, NR, true, false));
    TEST_ASSERT_TRUE(wxPinAllowed(25, R, NR, false, false));
}

static void test_negative_reserved_entries_ignored(void)
{
    TEST_ASSERT_TRUE(wxPinAllowed(1, R, NR, false, false));
    TEST_ASSERT_FALSE(wxPinAllowed(-1, R, NR, false, false));
}

static void test_flash_pins_classic(void)
{
    for (int p = 6; p <= 11; p++) TEST_ASSERT_FALSE(wxPinAllowed(p, NULL, 0, false, false));
    TEST_ASSERT_TRUE(wxPinAllowed(26, NULL, 0, false, false));
    TEST_ASSERT_TRUE(wxPinAllowed(12, NULL, 0, false, false));
}

static void test_flash_pins_s3(void)
{
    for (int p = 26; p <= 32; p++) TEST_ASSERT_FALSE(wxPinAllowed(p, NULL, 0, true, false));
    TEST_ASSERT_TRUE(wxPinAllowed(8, NULL, 0, true, false));   // 6-11 are ordinary GPIOs on the S3
    TEST_ASSERT_TRUE(wxPinAllowed(33, NULL, 0, true, false));
}

static void test_input_only_pins_classic_only(void)
{
    for (int p = 34; p <= 39; p++) TEST_ASSERT_FALSE(wxPinAllowed(p, NULL, 0, false, false));
    TEST_ASSERT_TRUE(wxPinAllowed(34, NULL, 0, true, false));
    TEST_ASSERT_TRUE(wxPinAllowed(33, NULL, 0, false, false));
}

static void test_octal_psram_pins_s3(void)
{
    // ESP32-S3 with octal PSRAM (N16R8): GPIO 33-37 carry the PSRAM data lines
    for (int p = 33; p <= 37; p++) TEST_ASSERT_FALSE(wxPinAllowed(p, NULL, 0, true, true));
    TEST_ASSERT_TRUE(wxPinAllowed(38, NULL, 0, true, true));
    TEST_ASSERT_TRUE(wxPinAllowed(21, NULL, 0, true, true));
    // quad PSRAM / no PSRAM: 33-37 are ordinary GPIOs
    for (int p = 33; p <= 37; p++) TEST_ASSERT_TRUE(wxPinAllowed(p, NULL, 0, true, false));
    // the flag has no meaning on the classic ESP32 (33 is fine, 34-37 stay input-only)
    TEST_ASSERT_TRUE(wxPinAllowed(33, NULL, 0, false, true));
    TEST_ASSERT_FALSE(wxPinAllowed(35, NULL, 0, false, true));
    // pin 0 = off stays accepted
    TEST_ASSERT_TRUE(wxPinAllowed(0, NULL, 0, true, true));
}

static void test_out_of_range(void)
{
    TEST_ASSERT_FALSE(wxPinAllowed(49, NULL, 0, true, false));
    TEST_ASSERT_TRUE(wxPinAllowed(48, NULL, 0, true, false));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_zero_is_off_and_always_allowed);
    RUN_TEST(test_reserved_board_pins_rejected);
    RUN_TEST(test_negative_reserved_entries_ignored);
    RUN_TEST(test_flash_pins_classic);
    RUN_TEST(test_flash_pins_s3);
    RUN_TEST(test_input_only_pins_classic_only);
    RUN_TEST(test_octal_psram_pins_s3);
    RUN_TEST(test_out_of_range);
    return UNITY_END();
}
