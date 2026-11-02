// WX-01 (upstream #999): LTR390 raw counts -> UVI. Sensitivity 2300 counts/UVI at gain 18x,
// 20-bit (400 ms); sensitivity = 2300 * (gain/18) * 2^(bits-20); UVI = raw / sensitivity * wfac.
#include <unity.h>

#include "ltr390_calc.h"

void setUp(void) {}
void tearDown(void) {}

// Hand calculations (tolerance 0.1):
//   0 counts                    -> 0 / 2300 = 0.0
//   2300, 18x, 20 bit           -> 2300 / 2300 = 1.0
//   23000, 18x, 20 bit          -> 23000 / 2300 = 10.0
//   4600, gain 9x, 20 bit       -> sens = 2300 * 9/18 = 1150; 4600 / 1150 = 4.0
//   1150, 18x, 19 bit           -> sens = 2300 * 0.5 = 1150; 1150 / 1150 = 1.0
//   2300, 18x, 20 bit, wfac 2.5 -> 1.0 * 2.5 = 2.5
//   34500, gain 3x, 18 bit      -> sens = 2300 * 3/18 * 0.25 = 95.8333; 34500 / 95.8333 = 360.0
void test_uvi_zero(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, ltr390Uvi(0, 18, 20, 1.0f));
}
void test_uvi_one(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, ltr390Uvi(2300, 18, 20, 1.0f));
}
void test_uvi_ten(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.0f, ltr390Uvi(23000, 18, 20, 1.0f));
}
void test_uvi_gain9(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 4.0f, ltr390Uvi(4600, 9, 20, 1.0f));
}
void test_uvi_19bit(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 1.0f, ltr390Uvi(1150, 18, 19, 1.0f));
}
void test_uvi_wfac(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 2.5f, ltr390Uvi(2300, 18, 20, 2.5f));
}
void test_uvi_gain3_18bit(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.5f, 360.0f, ltr390Uvi(34500, 3, 18, 1.0f));
}
void test_uvi_bad_gain_is_zero(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ltr390Uvi(2300, 0, 20, 1.0f));
}

// Only 16..20 bit exist usably on this part; 13 bit would be 12.5 ms (factor 1/32), 14/15 do not exist.
//   2300 counts, 18x, 16 bit -> sens = 2300 / 16 = 143.75; 2300 / 143.75 = 16.0
void test_uvi_16bit_scales_one_sixteenth(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.1f, 16.0f, ltr390Uvi(2300, 18, 16, 1.0f));
}
void test_uvi_invalid_resolution_is_zero(void)
{
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ltr390Uvi(2300, 18, 21, 1.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ltr390Uvi(2300, 18, 15, 1.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ltr390Uvi(2300, 18, 14, 1.0f));
	TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, ltr390Uvi(2300, 18, 13, 1.0f));
}
void test_meas_rate_is_20bit_500ms(void)
{
	// MEAS_RATE: bits 6:4 resolution (0 = 20 bit), bits 2:0 rate (4 = 500 ms)
	TEST_ASSERT_EQUAL_HEX8(0x04, LTR390_MEAS_20BIT_500MS);
}

void test_raw20_assembly(void)
{
	TEST_ASSERT_EQUAL_UINT32(0x030201u, ltr390Raw20(0x01, 0x02, 0x03));
	TEST_ASSERT_EQUAL_UINT32(0x0FFFFFu, ltr390Raw20(0xFF, 0xFF, 0xFF));  // top nibble masked
	TEST_ASSERT_EQUAL_UINT32(0x0u, ltr390Raw20(0x00, 0x00, 0xF0));       // only upper bits set
}

void test_part_id(void)
{
	TEST_ASSERT_TRUE(ltr390PartIdOk(0xB2));
	TEST_ASSERT_TRUE(ltr390PartIdOk(0xBF));
	TEST_ASSERT_FALSE(ltr390PartIdOk(0x00));
	TEST_ASSERT_FALSE(ltr390PartIdOk(0xFF));
	TEST_ASSERT_FALSE(ltr390PartIdOk(0x2B));
}

void test_data_ready(void)
{
	TEST_ASSERT_TRUE(ltr390DataReady(0x08));
	TEST_ASSERT_FALSE(ltr390DataReady(0x10));
}

int main(int, char **)
{
	UNITY_BEGIN();
	RUN_TEST(test_uvi_zero);
	RUN_TEST(test_uvi_one);
	RUN_TEST(test_uvi_ten);
	RUN_TEST(test_uvi_gain9);
	RUN_TEST(test_uvi_19bit);
	RUN_TEST(test_uvi_wfac);
	RUN_TEST(test_uvi_gain3_18bit);
	RUN_TEST(test_uvi_bad_gain_is_zero);
	RUN_TEST(test_uvi_16bit_scales_one_sixteenth);
	RUN_TEST(test_uvi_invalid_resolution_is_zero);
	RUN_TEST(test_meas_rate_is_20bit_500ms);
	RUN_TEST(test_raw20_assembly);
	RUN_TEST(test_part_id);
	RUN_TEST(test_data_ready);
	return UNITY_END();
}
