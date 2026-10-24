// #1182: on the Heltec Wireless Tracker the TFT backlight never went off,
// because every displayTFT() re-lit TFT_BL with node_contrast regardless of
// bDisplayIsOff. This compiles the real src/tft_display_functions.cpp
// (HAS_TFT block) against a no-op TFT_eSPI stub and an analogWrite recorder.

#include <unity.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

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
extern bool bDisplayOff;

// AU-21: ESP heap stand-in with settable values.
static struct
{
    uint32_t freeHeap = 111000;
    uint32_t maxAlloc = 55000;
    uint32_t getFreeHeap() const { return freeHeap; }
    uint32_t getMaxAllocHeap() const { return maxAlloc; }
} ESP;

// AU-21: printfdeb recorder. test/support/printfdeb_functions.h is a no-op
// shim; pull it in first (pragma once), then redirect the calls.
#include <printfdeb_functions.h>
static char g_logLine[256];
static int g_logCount = 0;
static int tft_test_printfdeb(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_logLine, sizeof(g_logLine), fmt, ap);
    va_end(ap);
    g_logCount++;
    return 0;
}
#define printfdeb tft_test_printfdeb

#include "../../src/tft_display_functions.cpp"

s_meshcom_settings meshcom_settings;
bool bDisplayIsOff = false;
bool bDisplayOff = false;

static void log_reset(void)
{
    g_logLine[0] = 0;
    g_logCount = 0;
}

void setUp(void)
{
    // Sprite present by default (the #1182 tests below draw into it).
    sprite.deleteSprite();
    sprite.createSprite(160, 80);
    g_tftStats.reset();
    log_reset();
    bDisplayOff = false;
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

// ---- AU-21: sprite on demand ----

static void test_init_display_off_creates_no_sprite(void)
{
    sprite.deleteSprite();
    g_tftStats.reset();
    bDisplayOff = true;
    initTFT();
    TEST_ASSERT_FALSE(tftSpriteActive());
    TEST_ASSERT_EQUAL_UINT32(0, tftSpriteBytes());
    TEST_ASSERT_EQUAL_INT(0, g_tftStats.createSprite);
    TEST_ASSERT_EQUAL_INT(0, g_logCount);
}

static void test_init_display_on_creates_sprite_once(void)
{
    sprite.deleteSprite();
    g_tftStats.reset();
    bDisplayOff = false;
    initTFT();
    TEST_ASSERT_TRUE(tftSpriteActive());
    TEST_ASSERT_EQUAL_UINT32(25600, tftSpriteBytes());
    TEST_ASSERT_EQUAL_INT(1, g_tftStats.createSprite);
    TEST_ASSERT_EQUAL_INT(1, g_logCount);
    TEST_ASSERT_NOT_NULL(strstr(g_logLine, "[TFT];sprite;alloc;25600;ok"));
}

static void test_acquire_is_idempotent(void)
{
    sprite.deleteSprite();
    g_tftStats.reset();
    TEST_ASSERT_TRUE(tftSpriteAcquire());
    TEST_ASSERT_EQUAL_INT(1, g_tftStats.createSprite);
    TEST_ASSERT_EQUAL_INT(1, g_logCount);
    TEST_ASSERT_TRUE(tftSpriteAcquire());
    TEST_ASSERT_EQUAL_INT(1, g_tftStats.createSprite);   // no new create
    TEST_ASSERT_EQUAL_INT(1, g_logCount);                // no new log
}

static void test_acquire_fail_logs_and_returns_false(void)
{
    sprite.deleteSprite();
    g_tftStats.reset();
    g_tftStats.failNextCreate = true;
    TEST_ASSERT_FALSE(tftSpriteAcquire());
    TEST_ASSERT_FALSE(tftSpriteActive());
    TEST_ASSERT_EQUAL_UINT32(0, tftSpriteBytes());
    TEST_ASSERT_EQUAL_INT(1, g_logCount);
    TEST_ASSERT_NOT_NULL(strstr(g_logLine, "[TFT];sprite;alloc;25600;fail"));
    // free heap / largest block are part of the line
    TEST_ASSERT_NOT_NULL(strstr(g_logLine, ";free;111000;blk;55000"));
}

static void test_release_frees_and_second_release_is_silent(void)
{
    TEST_ASSERT_TRUE(tftSpriteActive());
    log_reset();
    tftSpriteRelease();
    TEST_ASSERT_FALSE(tftSpriteActive());
    TEST_ASSERT_EQUAL_UINT32(0, tftSpriteBytes());
    TEST_ASSERT_EQUAL_INT(1, g_tftStats.deleteSprite);
    TEST_ASSERT_EQUAL_INT(1, g_logCount);
    TEST_ASSERT_NOT_NULL(strstr(g_logLine, "[TFT];sprite;free;25600"));

    log_reset();
    int deletes = g_tftStats.deleteSprite;
    tftSpriteRelease();
    TEST_ASSERT_EQUAL_INT(0, g_logCount);
    TEST_ASSERT_EQUAL_INT(deletes, g_tftStats.deleteSprite);
    TEST_ASSERT_FALSE(tftSpriteActive());
}

static void test_display_tft_released_is_noop_all_overloads(void)
{
    tftSpriteRelease();
    g_duty[TFT_BL] = -1;
    g_tftStats.reset();

    displayTFT("hdr");
    displayTFT("hdr", "line");
    displayTFT("hdr", "a", "b", "c", "d", 0);
    displayTFT("hdr", "a", "b", "c", "d", "e", 0);

    TEST_ASSERT_EQUAL_INT(0, g_tftStats.fillSprite);
    TEST_ASSERT_EQUAL_INT(0, g_tftStats.pushSprite);
    TEST_ASSERT_EQUAL_INT(-1, g_duty[TFT_BL]);   // backlight untouched
    TEST_ASSERT_FALSE(tftSpriteActive());        // displayTFT never re-creates
    TEST_ASSERT_EQUAL_INT(0, g_tftStats.createSprite);
}

static void test_display_tft_with_sprite_pushes_once_per_call(void)
{
    g_tftStats.reset();
    displayTFT("hdr");
    TEST_ASSERT_EQUAL_INT(1, g_tftStats.pushSprite);
    displayTFT("hdr", "line");
    TEST_ASSERT_EQUAL_INT(2, g_tftStats.pushSprite);
    displayTFT("hdr", "a", "b", "c", "d", 0);
    TEST_ASSERT_EQUAL_INT(3, g_tftStats.pushSprite);
    displayTFT("hdr", "a", "b", "c", "d", "e", 0);
    TEST_ASSERT_EQUAL_INT(4, g_tftStats.pushSprite);
    TEST_ASSERT_EQUAL_INT(200, g_duty[TFT_BL]);
}

static void test_show_updating_works_without_sprite(void)
{
    tftSpriteRelease();
    g_tftStats.reset();
    tftShowUpdating();
    TEST_ASSERT_EQUAL_INT(1, g_tftStats.drawString);
    TEST_ASSERT_EQUAL_STRING("Update...", g_tftStats.lastString.c_str());
    TEST_ASSERT_EQUAL_INT(0, g_tftStats.createSprite);
    TEST_ASSERT_EQUAL_INT(0, g_tftStats.pushSprite);
    TEST_ASSERT_EQUAL_INT(200, g_duty[TFT_BL]);   // backlight lit via tftBacklight()
    TEST_ASSERT_FALSE(tftSpriteActive());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_off_writes_zero_duty);
    RUN_TEST(test_off_writes_zero_duty_all_overloads);
    RUN_TEST(test_on_writes_contrast);
    RUN_TEST(test_wake_relights_after_off);
    RUN_TEST(test_init_display_off_creates_no_sprite);
    RUN_TEST(test_init_display_on_creates_sprite_once);
    RUN_TEST(test_acquire_is_idempotent);
    RUN_TEST(test_acquire_fail_logs_and_returns_false);
    RUN_TEST(test_release_frees_and_second_release_is_silent);
    RUN_TEST(test_display_tft_released_is_noop_all_overloads);
    RUN_TEST(test_display_tft_with_sprite_pushes_once_per_call);
    RUN_TEST(test_show_updating_works_without_sprite);
    return UNITY_END();
}
