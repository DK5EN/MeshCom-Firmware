// Native Testsuite fuer TBEAM_FIRST_BOOT_SSET (src/configuration_global.h) --
// den node_sset, den ein T-Beam beim ersten Start mit Werkseinstellungen
// bekommt (esp32_main.cpp, MODUL_FW_TBEAM-Block). TRK-03: seit V4.21 stand
// dort 0x0035; der Kommentar versprach "bGPSON = true", gesetzt wurde aber
// 0x0020 = Track. Ein frisch geflashter T-Beam startete mit Track an und GPS
// aus (Bench DK5EN-92, 2026-10-24).
//
//   pio test -e native -f test_tbeam_first_boot_sset

#include <unity.h>

#include <configuration_global.h>

// Bit-Belegung von node_sset laut esp32_main.cpp (bPosDisplay ... bGPSON).
static const int SSET_VOLT = 0x0001;
static const int SSET_POS = 0x0004;
static const int SSET_BUTTON = 0x0010;
static const int SSET_TRACK = 0x0020;
static const int SSET_GPS = 0x0040;

void setUp(void) {}
void tearDown(void) {}

// Der Kernfehler: Track darf nach dem ersten Start nicht an sein.
static void test_first_boot_leaves_track_off(void)
{
    TEST_ASSERT_EQUAL_HEX(0, TBEAM_FIRST_BOOT_SSET & SSET_TRACK);
}

// Was der Kommentar seit V4.21 verspricht: GPS an.
static void test_first_boot_turns_gps_on(void)
{
    TEST_ASSERT_EQUAL_HEX(SSET_GPS, TBEAM_FIRST_BOOT_SSET & SSET_GPS);
}

// Unveraendert gegenueber 0x0035: Volt-Anzeige, Positionsanzeige, Knopf.
static void test_first_boot_keeps_volt_pos_button(void)
{
    const int keep = SSET_VOLT | SSET_POS | SSET_BUTTON;
    TEST_ASSERT_EQUAL_HEX(keep, TBEAM_FIRST_BOOT_SSET & keep);
}

// Keine weiteren Bits ausser den vier oben.
static void test_first_boot_sets_nothing_else(void)
{
    const int known = SSET_VOLT | SSET_POS | SSET_BUTTON | SSET_GPS;
    TEST_ASSERT_EQUAL_HEX(0, TBEAM_FIRST_BOOT_SSET & ~known);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_boot_leaves_track_off);
    RUN_TEST(test_first_boot_turns_gps_on);
    RUN_TEST(test_first_boot_keeps_volt_pos_button);
    RUN_TEST(test_first_boot_sets_nothing_else);
    return UNITY_END();
}
