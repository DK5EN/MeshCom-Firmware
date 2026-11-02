// BAT-04 (upstream #1073): host tests for the pure core in src/batt_charge.h.
#include <unity.h>

#include <math.h>
#include <string.h>

#include "batt_charge.h"

void setUp(void) {}
void tearDown(void) {}

static void test_classify_all_eight_combos(void)
{
    // vbus, batt, charging
    TEST_ASSERT_EQUAL(BCS_UNKNOWN,     battChargeClassify(false, false, false));
    TEST_ASSERT_EQUAL(BCS_UNKNOWN,     battChargeClassify(false, false, true));
    TEST_ASSERT_EQUAL(BCS_DISCHARGING, battChargeClassify(false, true,  false));
    TEST_ASSERT_EQUAL(BCS_DISCHARGING, battChargeClassify(false, true,  true));
    TEST_ASSERT_EQUAL(BCS_NO_BATT,     battChargeClassify(true,  false, false));
    TEST_ASSERT_EQUAL(BCS_NO_BATT,     battChargeClassify(true,  false, true));
    TEST_ASSERT_EQUAL(BCS_FULL,        battChargeClassify(true,  true,  false));
    TEST_ASSERT_EQUAL(BCS_CHARGING,    battChargeClassify(true,  true,  true));
}

static void test_net_basic_and_sign(void)
{
    TEST_ASSERT_EQUAL_INT16(312,  battNetMa(312.0f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-87,  battNetMa(0.0f, 87.0f));
    TEST_ASSERT_EQUAL_INT16(0,    battNetMa(0.0f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(100,  battNetMa(150.0f, 50.0f));
}

static void test_net_rounding_half_away_from_zero(void)
{
    TEST_ASSERT_EQUAL_INT16(3,  battNetMa(2.5f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-3, battNetMa(0.0f, 2.5f));
    TEST_ASSERT_EQUAL_INT16(2,  battNetMa(2.4f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-2, battNetMa(0.0f, 2.4f));
    TEST_ASSERT_EQUAL_INT16(3,  battNetMa(2.6f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-3, battNetMa(0.0f, 2.6f));
    TEST_ASSERT_EQUAL_INT16(1,  battNetMa(0.5f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-1, battNetMa(0.0f, 0.5f));
    TEST_ASSERT_EQUAL_INT16(0,  battNetMa(0.4f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(0,  battNetMa(0.0f, 0.4f));
}

static void test_net_clamp_int16(void)
{
    TEST_ASSERT_EQUAL_INT16(32767,  battNetMa(40000.0f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, battNetMa(0.0f, 40000.0f));
    TEST_ASSERT_EQUAL_INT16(32767,  battNetMa(32767.0f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, battNetMa(0.0f, 32768.0f));
    TEST_ASSERT_EQUAL_INT16(32767,  battNetMa(1e30f, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, battNetMa(0.0f, 1e30f));
    TEST_ASSERT_EQUAL_INT16(32767,  battNetMa(INFINITY, 0.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, battNetMa(0.0f, INFINITY));
}

static void test_net_nan_is_zero(void)
{
    TEST_ASSERT_EQUAL_INT16(0, battNetMa(NAN, 0.0f));
    TEST_ASSERT_EQUAL_INT16(0, battNetMa(0.0f, NAN));
    TEST_ASSERT_EQUAL_INT16(0, battNetMa(NAN, NAN));
    TEST_ASSERT_EQUAL_INT16(0, battNetMa(100.0f, NAN));
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("unknown",     battChargeName(BCS_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("no battery",  battChargeName(BCS_NO_BATT));
    TEST_ASSERT_EQUAL_STRING("charging",    battChargeName(BCS_CHARGING));
    TEST_ASSERT_EQUAL_STRING("full",        battChargeName(BCS_FULL));
    TEST_ASSERT_EQUAL_STRING("discharging", battChargeName(BCS_DISCHARGING));
    TEST_ASSERT_EQUAL_STRING("unknown",     battChargeName((BattChargeState)99));
    TEST_ASSERT_EQUAL_STRING("unknown",     battChargeName((BattChargeState)255));
}

static size_t fmt(char *buf, size_t n, BattChargeState s, bool valid, int16_t ma)
{
    BattChargeInfo ci;
    ci.state = s;
    ci.ma_valid = valid;
    ci.ma = ma;
    return battChargeFormat(buf, n, ci);
}

static void test_format_without_current(void)
{
    char b[64];
    size_t r = fmt(b, sizeof(b), BCS_DISCHARGING, false, -87);
    TEST_ASSERT_EQUAL_STRING("discharging", b);
    TEST_ASSERT_EQUAL_UINT(strlen(b), r);
    r = fmt(b, sizeof(b), BCS_UNKNOWN, false, 0);
    TEST_ASSERT_EQUAL_STRING("unknown", b);
    TEST_ASSERT_EQUAL_UINT(7, r);
    r = fmt(b, sizeof(b), BCS_CHARGING, false, 312);
    TEST_ASSERT_EQUAL_STRING("charging", b);
}

static void test_format_with_current(void)
{
    char b[64];
    size_t r = fmt(b, sizeof(b), BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_STRING("charging +312 mA", b);
    TEST_ASSERT_EQUAL_UINT(strlen(b), r);
    r = fmt(b, sizeof(b), BCS_DISCHARGING, true, -87);
    TEST_ASSERT_EQUAL_STRING("discharging -87 mA", b);
    TEST_ASSERT_EQUAL_UINT(strlen(b), r);
    r = fmt(b, sizeof(b), BCS_FULL, true, 0);
    TEST_ASSERT_EQUAL_STRING("full 0 mA", b);
    TEST_ASSERT_EQUAL_UINT(strlen(b), r);
    r = fmt(b, sizeof(b), BCS_CHARGING, true, 32767);
    TEST_ASSERT_EQUAL_STRING("charging +32767 mA", b);
    r = fmt(b, sizeof(b), BCS_DISCHARGING, true, -32768);
    TEST_ASSERT_EQUAL_STRING("discharging -32768 mA", b);
    TEST_ASSERT_EQUAL_UINT(strlen(b), r);
}

static void test_format_truncation(void)
{
    char b[64];
    memset(b, 'X', sizeof(b));
    size_t r = fmt(b, 1, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_UINT(0, r);
    TEST_ASSERT_EQUAL_STRING("", b);
    TEST_ASSERT_EQUAL_CHAR('X', b[1]);

    memset(b, 'X', sizeof(b));
    r = fmt(b, 5, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_STRING("char", b);
    TEST_ASSERT_EQUAL_UINT(4, r);
    TEST_ASSERT_EQUAL_CHAR('X', b[5]);

    // cut inside the current part
    memset(b, 'X', sizeof(b));
    r = fmt(b, 13, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_STRING("charging +31", b);
    TEST_ASSERT_EQUAL_UINT(12, r);

    // exact fit and one short
    memset(b, 'X', sizeof(b));
    r = fmt(b, 17, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_STRING("charging +312 mA", b);
    TEST_ASSERT_EQUAL_UINT(16, r);
    r = fmt(b, 16, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_STRING("charging +312 m", b);
    TEST_ASSERT_EQUAL_UINT(15, r);
}

static void test_format_n_zero_and_null(void)
{
    char b[4] = {'X', 'X', 'X', 'X'};
    size_t r = fmt(b, 0, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_UINT(0, r);
    TEST_ASSERT_EQUAL_CHAR('X', b[0]);
    r = fmt(NULL, 10, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_UINT(0, r);
    r = fmt(NULL, 0, BCS_CHARGING, true, 312);
    TEST_ASSERT_EQUAL_UINT(0, r);
}

// BAT-04b: BQ25896 + BQ27220 mapping (T-Deck Pro, T5 e-paper).
static void test_bq_charging(void)
{
    BattChargeInfo c = battChargeFromBq(true, true, true, 412);
    TEST_ASSERT_EQUAL(BCS_CHARGING, c.state);
    TEST_ASSERT_TRUE(c.ma_valid);
    TEST_ASSERT_EQUAL_INT16(412, c.ma);
}
static void test_bq_full(void)
{
    BattChargeInfo c = battChargeFromBq(true, true, false, 3);
    TEST_ASSERT_EQUAL(BCS_FULL, c.state);
    TEST_ASSERT_TRUE(c.ma_valid);
    TEST_ASSERT_EQUAL_INT16(3, c.ma);
}
static void test_bq_discharging_signed(void)
{
    BattChargeInfo c = battChargeFromBq(false, true, false, -87);
    TEST_ASSERT_EQUAL(BCS_DISCHARGING, c.state);
    TEST_ASSERT_TRUE(c.ma_valid);
    TEST_ASSERT_EQUAL_INT16(-87, c.ma);
}
static void test_bq_charging_flag_ignored_without_vbus(void)
{
    BattChargeInfo c = battChargeFromBq(false, true, true, -50);
    TEST_ASSERT_EQUAL(BCS_DISCHARGING, c.state);
}
// BAT-04b: cell presence from the BQ27220 BatteryStatus BATTPRES bit (bit 3), not from the voltage
// (on USB-only power the charger holds BAT up, so the gauge voltage alone reads "present").
static void test_bq_present_uses_battpres_bit(void)
{
    TEST_ASSERT_TRUE(battBqPresent(0x0008, 3900));
    TEST_ASSERT_TRUE(battBqPresent(0x0008 | 0x0001, 0));      // bit wins over the voltage
    TEST_ASSERT_FALSE(battBqPresent(0x0000, 4150));           // USB only, charger holds BAT: no cell
    TEST_ASSERT_FALSE(battBqPresent(0x0001 | 0x0010, 4200));  // other bits set, BATTPRES clear
}
static void test_bq_present_falls_back_to_voltage_on_failed_read(void)
{
    TEST_ASSERT_TRUE(battBqPresent(0xFFFF, 3700));            // all-ones = bus read failed
    TEST_ASSERT_FALSE(battBqPresent(0xFFFF, 0xFFFF));         // failed read: voltage reads 65535 mV too, no cell
    TEST_ASSERT_TRUE(battBqPresent(0xFFFF, 2500));
    TEST_ASSERT_FALSE(battBqPresent(0xFFFF, 2499));
    TEST_ASSERT_FALSE(battBqPresent(0xFFFF, 0));
}
static void test_bq_no_battery(void)
{
    BattChargeInfo c = battChargeFromBq(true, false, false, 0);
    TEST_ASSERT_EQUAL(BCS_NO_BATT, c.state);
    TEST_ASSERT_FALSE(c.ma_valid);
    c = battChargeFromBq(false, false, false, 0);
    TEST_ASSERT_EQUAL(BCS_UNKNOWN, c.state);
    TEST_ASSERT_FALSE(c.ma_valid);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_classify_all_eight_combos);
    RUN_TEST(test_net_basic_and_sign);
    RUN_TEST(test_net_rounding_half_away_from_zero);
    RUN_TEST(test_net_clamp_int16);
    RUN_TEST(test_net_nan_is_zero);
    RUN_TEST(test_names);
    RUN_TEST(test_format_without_current);
    RUN_TEST(test_format_with_current);
    RUN_TEST(test_format_truncation);
    RUN_TEST(test_format_n_zero_and_null);
    RUN_TEST(test_bq_charging);
    RUN_TEST(test_bq_full);
    RUN_TEST(test_bq_discharging_signed);
    RUN_TEST(test_bq_charging_flag_ignored_without_vbus);
    RUN_TEST(test_bq_no_battery);
    RUN_TEST(test_bq_present_uses_battpres_bit);
    RUN_TEST(test_bq_present_falls_back_to_voltage_on_failed_read);
    return UNITY_END();
}
