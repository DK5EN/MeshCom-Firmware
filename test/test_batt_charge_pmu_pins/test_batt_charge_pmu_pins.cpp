// BAT-04 (upstream #1073): source pins on the ESP32 PMU read path. Reads
// src/esp32/loop_actions_esp32.cpp and src/esp32/esp32_pmu.cpp as text (like
// test_drift_dr16_gates) and pins:
//   * the MODUL_FW_TBEAM branch of loopAction_battCheck() reads isVbusIn, isBatteryConnect,
//     isCharging, classifies with battChargeClassify() and stores global_batt_charge;
//   * the AXP192-only static_cast<XPowersAXP192 *> sits behind a getChipModel() == XPOWERS_AXP192
//     check (AXP2101 has no battery current ADC);
//   * setupPMU() enables the battery measurement ADC in its AXP192 branch.
//
//   pio test -e native_batt_charge -f test_batt_charge_pmu_pins
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

void setUp(void)    {}
void tearDown(void) {}

static std::string read_rel(const char *rel)
{
    static const char *prefixes[] = { "", "../", "../../", "../../../", "../../../../" };
    char path[512];
    for (const char *p : prefixes)
    {
        snprintf(path, sizeof(path), "%s%s", p, rel);
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        std::string s;
        char b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), f)) > 0)
            s.append(b, n);
        fclose(f);
        return s;
    }
    TEST_FAIL_MESSAGE("source file not found");
    return std::string();
}

// The MODUL_FW_TBEAM branch of loopAction_battCheck(): from its #elif to the next #else.
static std::string tbeam_branch(void)
{
    std::string s = read_rel("src/esp32/loop_actions_esp32.cpp");
    size_t a = s.find("#elif defined(MODUL_FW_TBEAM)");
    TEST_ASSERT_TRUE_MESSAGE(a != std::string::npos, "MODUL_FW_TBEAM #elif branch missing");
    size_t b = s.find("#else", a);
    TEST_ASSERT_TRUE_MESSAGE(b != std::string::npos, "end of MODUL_FW_TBEAM branch missing");
    return s.substr(a, b - a);
}

static void has(const std::string &hay, const char *needle)
{
    TEST_ASSERT_TRUE_MESSAGE(hay.find(needle) != std::string::npos, needle);
}

void test_tbeam_branch_reads_pmu_flags(void)
{
    std::string br = tbeam_branch();
    has(br, "isVbusIn");
    has(br, "isBatteryConnect");
    has(br, "isCharging");
    has(br, "battChargeClassify");
}

void test_tbeam_branch_stores_global(void)
{
    has(tbeam_branch(), "global_batt_charge =");
}

void test_axp192_cast_behind_chip_check(void)
{
    std::string br = tbeam_branch();
    size_t chk = br.find("getChipModel() == XPOWERS_AXP192");
    TEST_ASSERT_TRUE_MESSAGE(chk != std::string::npos, "no getChipModel() == XPOWERS_AXP192 check");
    size_t cast = br.find("static_cast<XPowersAXP192 *>");
    TEST_ASSERT_TRUE_MESSAGE(cast != std::string::npos, "no static_cast<XPowersAXP192 *>");
    TEST_ASSERT_TRUE_MESSAGE(chk < cast, "cast precedes the AXP192 chip check");
    // the cast must be in the block opened right after the check, not past some closed one
    size_t open = br.find('{', chk);
    TEST_ASSERT_TRUE_MESSAGE(open != std::string::npos && open < cast, "cast not inside the check's block");
    int depth = 0;
    for (size_t i = open; i < cast; i++)
    {
        if (br[i] == '{') depth++;
        else if (br[i] == '}') depth--;
    }
    TEST_ASSERT_TRUE_MESSAGE(depth >= 1, "cast is outside the AXP192 check's block");
    has(br, "getBatteryChargeCurrent");
    has(br, "getBattDischargeCurrent");
    has(br, "battNetMa");
}

void test_setuppmu_axp192_enables_batt_adc(void)
{
    std::string s = read_rel("src/esp32/esp32_pmu.cpp");
    size_t a = s.find("== XPOWERS_AXP192)");
    TEST_ASSERT_TRUE_MESSAGE(a != std::string::npos, "AXP192 check missing in setupPMU");
    size_t b = s.find("XPOWERS_AXP2101", a);
    TEST_ASSERT_TRUE_MESSAGE(b != std::string::npos, "AXP2101 check missing after AXP192 branch");
    std::string blk = s.substr(a, b - a);
    has(blk, "enableBattVoltageMeasure");
    // the VBUS ADC stays as it was: enabling it would turn a cell-less T-Beam's "USB" (global_batt
    // == 0 from getVbusVoltage()) into "5.0V" -- isVbusIn() reads a status bit, not the ADC
    TEST_ASSERT_TRUE_MESSAGE(blk.find("enableVbusVoltageMeasure") == std::string::npos,
                             "AXP192 branch must not enable the VBUS ADC");
}

// A failed I2C read makes readRegisterH8L5() return 0xFFFF (32767.5 mA); a real 13-bit reading
// tops out at 4095.5 mA. Both readings must pass that bound before ma_valid is set.
void test_axp192_current_plausibility_bound(void)
{
    std::string br = tbeam_branch();
    size_t bound = br.find("<= 4095.5f");
    TEST_ASSERT_TRUE_MESSAGE(bound != std::string::npos, "no 4095.5 mA plausibility bound");
    size_t valid = br.find("ma_valid = true");
    TEST_ASSERT_TRUE_MESSAGE(valid != std::string::npos && bound < valid, "ma_valid set before the bound check");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_tbeam_branch_reads_pmu_flags);
    RUN_TEST(test_tbeam_branch_stores_global);
    RUN_TEST(test_axp192_cast_behind_chip_check);
    RUN_TEST(test_setuppmu_axp192_enables_batt_adc);
    RUN_TEST(test_axp192_current_plausibility_bound);
    return UNITY_END();
}
