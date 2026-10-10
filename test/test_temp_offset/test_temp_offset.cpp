// Native regression test for the temperature offsets of the AHT20 and SHT21
// drivers (src/aht20.cpp, src/sht21.cpp).
//
// Bug (2026-10-10, field report): "Indoor Temp Offset" in the web GUI and
// "--tempoff in" had no effect on an AHT20. BMX280, BMP390 and BME680 add
// node_tempi_off when they read the sensor; aht20.cpp stored the raw value,
// and sht21.cpp likewise ignored node_tempo_off for its outdoor reading
// (node_temp2).
//
//   pio test -e native_temp_offset

#include <unity.h>

#include <configuration.h>
#include <loop_functions_extern.h>
#include <meshcom_settings.h>

#include <Adafruit_AHTX0.h>
#include <SHTSensor.h>

#include <aht20.h>
#include <sht21.h>

s_meshcom_settings meshcom_settings;
bool bAHT20ON = false;
bool aht20_found = false;
bool bSHT21ON = false;
bool sht21_found = false;
bool bWXDEBUG = false;

void setUp(void)
{
    meshcom_settings = s_meshcom_settings();
    bAHT20ON = true;
    bSHT21ON = true;
    bWXDEBUG = false;
    setupAHT20(true);
    setupSHT21(true);
}

void tearDown(void) {}

static void test_aht20_applies_indoor_offset(void)
{
    mc_test_aht_temp = 21.8f;
    meshcom_settings.node_tempi_off = -0.8f;

    TEST_ASSERT_TRUE(loopAHT20());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.0f, getAHT20Temp());
}

static void test_aht20_ignores_outdoor_offset(void)
{
    mc_test_aht_temp = 21.8f;
    meshcom_settings.node_tempo_off = 5.0f;

    TEST_ASSERT_TRUE(loopAHT20());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.8f, getAHT20Temp());
}

static void test_aht20_humidity_untouched(void)
{
    mc_test_aht_hum = 55.0f;
    meshcom_settings.node_tempi_off = -0.8f;

    TEST_ASSERT_TRUE(loopAHT20());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 55.0f, getAHT20Hum());
}

static void test_sht21_applies_outdoor_offset(void)
{
    mc_test_sht_temp = 10.0f;
    meshcom_settings.node_tempo_off = 1.5f;

    TEST_ASSERT_TRUE(loopSHT21());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 11.5f, getSHT21Temp());
}

static void test_sht21_ignores_indoor_offset(void)
{
    mc_test_sht_temp = 10.0f;
    meshcom_settings.node_tempi_off = 5.0f;

    TEST_ASSERT_TRUE(loopSHT21());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, getSHT21Temp());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_aht20_applies_indoor_offset);
    RUN_TEST(test_aht20_ignores_outdoor_offset);
    RUN_TEST(test_aht20_humidity_untouched);
    RUN_TEST(test_sht21_applies_outdoor_offset);
    RUN_TEST(test_sht21_ignores_indoor_offset);
    return UNITY_END();
}
