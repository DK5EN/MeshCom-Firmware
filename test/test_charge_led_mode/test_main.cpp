// LED W3 CHG-01/CHG-02: boot charge-LED mode from node_sset2 / node_sset3 (src/charge_led_mode.h, header-only).
//
//   pio test -e native_charge_led_mode
//
// Before the fix the boot read `bCHARGE_LED = node_sset3 & 0x0001` and called setChargeLedOn(bCHARGE_LED):
// a fresh node (sset3 = 0) came up with the AXP192 LED dark, and a stale node whose bit 0x0001 was left
// over from the old bMHONLY flag came up with the LED "on" without anyone configuring it.
// oldBoot() is that rule, kept here as the comparison.
//
// The "configured" marker is node_sset2 bit 0x8000: inside the 0..65535 settings range of config_json.h, so
// it survives the load clamp and a downgrade (a marker above bit 15 of sset3 would clamp to 65535 = all flags on).

#include <unity.h>

#include "charge_led_mode.h"

void setUp(void) {}
void tearDown(void) {}

// the pre-fix rule: true = PMU auto mode, false = dark
static bool oldBoot(int sset3) { return (sset3 & 0x0001) != 0; }

// sset2 bits in use in src/ (command_functions.cpp toggle rows and setters), documented here instead of grepped:
//   0x0001 0x0002 0x0004 0x0008 0x0020 0x0040 0x0080 0x0100 0x0200 0x0400 0x0800 0x1000 0x4000
// 0x2000 is a commented-out placeholder (bTRACK_old) and stays free; 0x8000 is the charge LED marker.
static const int SSET2_BITS_IN_USE = 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0020 | 0x0040 | 0x0080 | 0x0100 |
                                     0x0200 | 0x0400 | 0x0800 | 0x1000 | 0x4000;

static void test_fresh_node_follows_charging(void)
{
    TEST_ASSERT_EQUAL_INT(CHGLED_AUTO, chargeLedModeFromSettings(0, 0));
    TEST_ASSERT_TRUE(chargeLedAutoFromSettings(0, 0));
    TEST_ASSERT_FALSE_MESSAGE(oldBoot(0), "the old rule left a fresh AXP192 node dark");
}

static void test_stale_sset3_bit_without_marker_is_ignored(void)
{
    // sset3 0x0001 alone is the old bMHONLY leftover; marker clear -> auto, whatever else is set
    TEST_ASSERT_EQUAL_INT(CHGLED_AUTO, chargeLedModeFromSettings(0, 0x0001));
    TEST_ASSERT_EQUAL_INT(CHGLED_AUTO, chargeLedModeFromSettings(0x7FFF, 0xFFFF));
    TEST_ASSERT_EQUAL_INT(CHGLED_AUTO, chargeLedModeFromSettings(SSET2_BITS_IN_USE, 0x8080));
    TEST_ASSERT_TRUE_MESSAGE(oldBoot(0x0001), "the old rule read the stale bit as 'on'");
}

static void test_marker_with_sset3_bit_is_on_meaning_follows_charging(void)
{
    TEST_ASSERT_EQUAL_INT(CHGLED_AUTO, chargeLedModeFromSettings(SSET2_CHGLED_CFG, SSET3_CHGLED_ON));
    TEST_ASSERT_TRUE(chargeLedAutoFromSettings(SSET2_CHGLED_CFG | SSET2_BITS_IN_USE, SSET3_CHGLED_ON | 0x8000));
}

static void test_marker_without_sset3_bit_is_off(void)
{
    TEST_ASSERT_EQUAL_INT(CHGLED_OFF, chargeLedModeFromSettings(SSET2_CHGLED_CFG, 0));
    TEST_ASSERT_FALSE(chargeLedAutoFromSettings(SSET2_CHGLED_CFG | SSET2_BITS_IN_USE, 0xFFFE));
}

// the console rows: "--charge led on" = sset3 | 0x0001, "--charge led off" = sset3 & ~1 (the original upstream
// masks), both post hooks do sset2 |= SSET2_CHGLED_CFG
static void test_console_rows_round_trip(void)
{
    const int start3[] = {0, 0x0001, 0x8000, 0xFFFF};
    const int start2[] = {0, SSET2_BITS_IN_USE, SSET2_CHGLED_CFG};
    for (int s3 : start3)
        for (int s2 : start2)
        {
            const int on3 = (s3 & 0xFFFFFFFF) | 0x0001;
            const int off3 = (s3 & 0xFFFFFFFE);
            const int cfg2 = s2 | SSET2_CHGLED_CFG;
            TEST_ASSERT_TRUE(chargeLedAutoFromSettings(cfg2, on3));
            TEST_ASSERT_FALSE(chargeLedAutoFromSettings(cfg2, off3));
            TEST_ASSERT_EQUAL_INT(s3 & 0xFFFE, on3 & 0xFFFE);   // the other sset3 bits stay
            TEST_ASSERT_EQUAL_INT(s3 & 0xFFFE, off3 & 0xFFFE);
            TEST_ASSERT_EQUAL_INT(s2 & ~SSET2_CHGLED_CFG, cfg2 & ~SSET2_CHGLED_CFG);   // the other sset2 bits stay
        }
}

// the marker lives in the 16-bit sset2 range (survives the config_json.h clamp, 0..65535) and is not a bit
// another sset2 setter owns
static void test_marker_bit_is_free_and_inside_the_16_bit_range(void)
{
    TEST_ASSERT_EQUAL_INT(0x8000, SSET2_CHGLED_CFG);
    TEST_ASSERT_EQUAL_INT(0, SSET2_CHGLED_CFG & SSET2_BITS_IN_USE);
    TEST_ASSERT_EQUAL_INT(0, SSET2_CHGLED_CFG & ~0xFFFF);
}

// a stored marker survives the settings clamp: values are clamped to 0..65535, the marker is inside
static void test_marker_survives_the_load_clamp(void)
{
    int sset2 = SSET2_CHGLED_CFG | 0x0004;
    const int clamped = sset2 < 0 ? 0 : sset2 > 65535 ? 65535 : sset2;
    TEST_ASSERT_EQUAL_INT(sset2, clamped);
    TEST_ASSERT_EQUAL_INT(CHGLED_OFF, chargeLedModeFromSettings(clamped, 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_fresh_node_follows_charging);
    RUN_TEST(test_stale_sset3_bit_without_marker_is_ignored);
    RUN_TEST(test_marker_with_sset3_bit_is_on_meaning_follows_charging);
    RUN_TEST(test_marker_without_sset3_bit_is_off);
    RUN_TEST(test_console_rows_round_trip);
    RUN_TEST(test_marker_bit_is_free_and_inside_the_16_bit_range);
    RUN_TEST(test_marker_survives_the_load_clamp);
    return UNITY_END();
}
