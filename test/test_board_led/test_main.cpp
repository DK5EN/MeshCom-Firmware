// LED W1: board LED logic (src/board_led.h, header-only, Arduino-free).
//
//   pio test -e native_board_led
//
// LED-04  T-Beam: BOARD_LED and OneWire_GPIO are both GPIO4; the heartbeat must yield while OneWire owns the pin.
// LED-11  the flasher starts dark: "lit" is a logical state, not a pin level (bLED = BOARD_LED_ACTIVE_LOW mixed both).
// 6.1     ledFitted(): the "not fitted" flags for BLED / CHGLED reporting.

#include <unity.h>

#include "board_led.h"

void setUp(void) {}
void tearDown(void) {}

static void test_pin_free_when_onewire_off(void)
{
    TEST_ASSERT_TRUE(boardLedPinFree(false, 4, 4));
    TEST_ASSERT_TRUE(boardLedPinFree(false, 25, 4));
}

static void test_pin_shared_with_onewire_is_not_free(void)
{
    TEST_ASSERT_FALSE(boardLedPinFree(true, 4, 4));   // T-Beam: OneWire_GPIO 4 == BOARD_LED 4
}

static void test_pin_free_when_onewire_uses_another_pin(void)
{
    TEST_ASSERT_TRUE(boardLedPinFree(true, 13, 4));   // e.g. --owgpio 13 on a T-Beam
}

static void test_no_led_pin_is_never_free(void)
{
    TEST_ASSERT_FALSE(boardLedPinFree(false, 4, -1));
    TEST_ASSERT_FALSE(boardLedPinFree(true, -1, -1));
}

static void test_onewire_pin_fallback(void)
{
    TEST_ASSERT_EQUAL_INT(13, boardLedOwPin(13, 4));   // stored node_owgpio wins
    TEST_ASSERT_EQUAL_INT(4, boardLedOwPin(0, 4));     // unset (0) -> compile-time OneWire_GPIO
    TEST_ASSERT_EQUAL_INT(4, boardLedOwPin(-1, 4));
    TEST_ASSERT_EQUAL_INT(-1, boardLedOwPin(0, -1));   // nothing known: no pin to collide with
    TEST_ASSERT_FALSE(boardLedPinFree(true, boardLedOwPin(0, 4), 4));
}

static void test_onewire_pin_tbeam_remap_16(void)
{
    // BOARD_TBEAM: the driver turns a stored 16 into 0 -> OneWire_GPIO (4); the helper maps it the same way
    TEST_ASSERT_EQUAL_INT(4, boardLedOwPin(16, 4, true));
    TEST_ASSERT_FALSE(boardLedPinFree(true, boardLedOwPin(16, 4, true), 4));   // collides with BOARD_LED 4
    TEST_ASSERT_EQUAL_INT(16, boardLedOwPin(16, 4, false));                    // other boards: 16 stays 16
    TEST_ASSERT_EQUAL_INT(13, boardLedOwPin(13, 4, true));                     // only 16 is remapped
}

static void test_flasher_starts_dark_and_lights_after_interval(void)
{
    BoardLedStep s = boardLedPhase(1000, 0, false);   // 1 s into the run: interval (2 s) not yet over
    TEST_ASSERT_FALSE(s.lit);
    TEST_ASSERT_FALSE(s.changed);
    s = boardLedPhase(2000, 0, false);
    TEST_ASSERT_TRUE(s.lit);
    TEST_ASSERT_TRUE(s.changed);
    TEST_ASSERT_EQUAL_UINT32(2000, s.timer);
}

static void test_flasher_goes_dark_after_flash_time(void)
{
    BoardLedStep s = boardLedPhase(2029, 2000, true);
    TEST_ASSERT_TRUE(s.lit);
    TEST_ASSERT_FALSE(s.changed);
    s = boardLedPhase(2030, 2000, true);
    TEST_ASSERT_FALSE(s.lit);
    TEST_ASSERT_TRUE(s.changed);
    TEST_ASSERT_EQUAL_UINT32(2000, s.timer);   // phase anchor stays: the next flash is 2 s after the last start
}

static void test_flasher_is_millis_wrap_safe(void)
{
    BoardLedStep s = boardLedPhase(1999u, 0xFFFFF000u, false);   // 4096 + 1999 ms after the anchor
    TEST_ASSERT_TRUE(s.lit);
    s = boardLedPhase(5u, 0xFFFFFFF0u, true);   // 21 ms after the anchor
    TEST_ASSERT_TRUE(s.lit);
    s = boardLedPhase(20u, 0xFFFFFFF0u, true);  // 36 ms
    TEST_ASSERT_FALSE(s.lit);
}

static void test_flasher_full_cycle_is_30ms_in_2s(void)
{
    uint32_t timer = 0;
    bool lit = false;
    uint32_t litMs = 0;
    for (uint32_t now = 0; now < 20000; now++)
    {
        BoardLedStep s = boardLedPhase(now, timer, lit);
        timer = s.timer;
        lit = s.lit;
        if (lit)
            litMs++;
    }
    TEST_ASSERT_EQUAL_UINT32(270, litMs);   // flashes at 2 s .. 18 s = 9 x 30 ms
}

static void test_level_follows_polarity(void)
{
    TEST_ASSERT_EQUAL_INT(1, boardLedLevel(true, false));
    TEST_ASSERT_EQUAL_INT(0, boardLedLevel(false, false));
    TEST_ASSERT_EQUAL_INT(0, boardLedLevel(true, true));    // active low: lit = LOW
    TEST_ASSERT_EQUAL_INT(1, boardLedLevel(false, true));
}

static void test_fitted_flags(void)
{
    LedFitted f = ledFitted(true, false, true, true);   // BOARD_LED, AXP compiled in, PMU found
    TEST_ASSERT_TRUE(f.board);
    TEST_ASSERT_TRUE(f.charge);
    f = ledFitted(false, true, false, false);           // NeoPixel only (S3 DevKitC), no AXP
    TEST_ASSERT_TRUE(f.board);
    TEST_ASSERT_FALSE(f.charge);
    f = ledFitted(false, false, true, false);           // AXP compiled in but no PMU answered
    TEST_ASSERT_FALSE(f.board);
    TEST_ASSERT_FALSE(f.charge);
    f = ledFitted(false, false, false, true);           // PMU pointer without AXP support compiled in
    TEST_ASSERT_FALSE(f.charge);
}

static void test_not_fitted_list(void)
{
    char buf[16];
    TEST_ASSERT_EQUAL_INT(0, ledNotFittedList(ledFitted(true, false, true, true), buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
    ledNotFittedList(ledFitted(false, false, false, false), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("BLED,CHGLED", buf);
    ledNotFittedList(ledFitted(true, false, false, false), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("CHGLED", buf);
    ledNotFittedList(ledFitted(false, false, true, true), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("BLED", buf);
    char tiny[4];   // never overruns
    ledNotFittedList(ledFitted(false, false, false, false), tiny, sizeof tiny);
    TEST_ASSERT_EQUAL_STRING("BLE", tiny);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_pin_free_when_onewire_off);
    RUN_TEST(test_pin_shared_with_onewire_is_not_free);
    RUN_TEST(test_pin_free_when_onewire_uses_another_pin);
    RUN_TEST(test_no_led_pin_is_never_free);
    RUN_TEST(test_onewire_pin_fallback);
    RUN_TEST(test_onewire_pin_tbeam_remap_16);
    RUN_TEST(test_flasher_starts_dark_and_lights_after_interval);
    RUN_TEST(test_flasher_goes_dark_after_flash_time);
    RUN_TEST(test_flasher_is_millis_wrap_safe);
    RUN_TEST(test_flasher_full_cycle_is_30ms_in_2s);
    RUN_TEST(test_level_follows_polarity);
    RUN_TEST(test_fitted_flags);
    RUN_TEST(test_not_fitted_list);
    return UNITY_END();
}
