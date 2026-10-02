// #1182: on the Heltec Wireless Tracker the TFT backlight never went off,
// because every displayTFT() re-lit TFT_BL with node_contrast regardless of
// bDisplayIsOff. This compiles the real src/tft_display_functions.cpp
// (HAS_TFT block) against a no-op TFT_eSPI stub and an analogWrite recorder.

#include <unity.h>
#include <string.h>

#define HAS_TFT 1
#define TFT_BL 21
#define OUTPUT 1

// Pin recorder: last analogWrite() per pin, -1 = never written.
static int g_duty[64];
static void pinMode(int, int) {}
static void analogWrite(int pin, int duty) { g_duty[pin] = duty; }

// loop_functions.h drags in the nRF52 WisBlock stub, whose settings struct
// lacks node_contrast; the file only needs the two globals below.
#define _LOOP_FUNCTIONS_H_
#include "../../src/meshcom_settings.h"
extern bool bDisplayIsOff;

#include "../../src/tft_display_functions.cpp"

s_meshcom_settings meshcom_settings;
bool bDisplayIsOff = false;

void setUp(void)
{
    for (int i = 0; i < 64; i++)
        g_duty[i] = -1;
    meshcom_settings.node_contrast = 200;
    bDisplayIsOff = false;
}
void tearDown(void) {}

static void test_off_writes_zero_duty(void)
{
    bDisplayIsOff = true;
    displayTFT("hdr", "line");
    TEST_ASSERT_EQUAL_INT(0, g_duty[TFT_BL]);
}

static void test_off_writes_zero_duty_all_overloads(void)
{
    bDisplayIsOff = true;
    displayTFT("hdr");
    TEST_ASSERT_EQUAL_INT(0, g_duty[TFT_BL]);
    g_duty[TFT_BL] = -1;
    displayTFT("hdr", "a", "b", "c", "d", 0);
    TEST_ASSERT_EQUAL_INT(0, g_duty[TFT_BL]);
    g_duty[TFT_BL] = -1;
    displayTFT("hdr", "a", "b", "c", "d", "e", 0);
    TEST_ASSERT_EQUAL_INT(0, g_duty[TFT_BL]);
}

static void test_on_writes_contrast(void)
{
    bDisplayIsOff = false;
    displayTFT("hdr", "line");
    TEST_ASSERT_EQUAL_INT(200, g_duty[TFT_BL]);
}

static void test_wake_relights_after_off(void)
{
    bDisplayIsOff = true;
    displayTFT("hdr");
    TEST_ASSERT_EQUAL_INT(0, g_duty[TFT_BL]);
    bDisplayIsOff = false;
    displayTFT("hdr");
    TEST_ASSERT_EQUAL_INT(200, g_duty[TFT_BL]);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_writes_zero_duty);
    RUN_TEST(test_off_writes_zero_duty_all_overloads);
    RUN_TEST(test_on_writes_contrast);
    RUN_TEST(test_wake_relights_after_off);
    return UNITY_END();
}
