/**
 * The numeric-argument helpers behind the D2-07 setters.
 *
 * The cases below are the ones the ladder actually gets wrong or relies on:
 * a non-numeric argument (which used to read an uninitialised temporary),
 * an argument that starts at the separator rather than past it, a value that
 * fails the range check (the destination must not be touched), and the
 * "no range" form that rungs without bounds use.
 */
#include <unity.h>
#include <string.h>

#include "command_setters.h"

static void test_a_plain_number_parses()
{
    TEST_ASSERT_EQUAL_INT(42, (int)cmdArgLong("42"));
    TEST_ASSERT_EQUAL_INT(-9, (int)cmdArgLong("-9"));
    TEST_ASSERT_EQUAL_DOUBLE(0.025, cmdArgDouble("0.025"));
}

// The bug this header exists for: `sscanf` left its target untouched, and the
// int temporary was uninitialised, so `--txpower abc` reported 16711680.
static void test_a_non_numeric_argument_is_zero_not_leftover()
{
    TEST_ASSERT_EQUAL_INT(0, (int)cmdArgLong("abc"));
    TEST_ASSERT_EQUAL_INT(0, (int)cmdArgLong(""));
    TEST_ASSERT_EQUAL_INT(0, (int)cmdArgLong(nullptr));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, cmdArgDouble("abc"));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, cmdArgDouble(nullptr));
}

// Several rungs aim at the separating space rather than past it; %s and %d both
// skip leading whitespace, so that has always worked and must keep working.
static void test_leading_space_is_skipped()
{
    TEST_ASSERT_EQUAL_INT(7, (int)cmdArgLong(" 7"));
    TEST_ASSERT_EQUAL_DOUBLE(1.5, cmdArgDouble("  1.5"));
}

static void test_trailing_junk_still_yields_the_leading_number()
{
    TEST_ASSERT_EQUAL_INT(12, (int)cmdArgLong("12abc"));   // as sscanf("%d") did
    TEST_ASSERT_EQUAL_INT(3, (int)cmdArgLong("3 4"));
}

// "%i" is auto-base and "%d" is not: two setters use "%i", where a leading zero
// means octal. Converting them to a decimal parse would silently change values.
static void test_percent_i_keeps_its_auto_base()
{
    TEST_ASSERT_EQUAL_INT(8, (int)cmdArgLongBase("010", 0));    // as "%i" reads it
    TEST_ASSERT_EQUAL_INT(16, (int)cmdArgLongBase("0x10", 0));
    TEST_ASSERT_EQUAL_INT(10, (int)cmdArgLong("010"));          // as "%d" reads it
}

static void test_range_is_inclusive()
{
    TEST_ASSERT_TRUE(cmdInRange(1, 1, 6));
    TEST_ASSERT_TRUE(cmdInRange(6, 1, 6));
    TEST_ASSERT_FALSE(cmdInRange(0, 1, 6));
    TEST_ASSERT_FALSE(cmdInRange(7, 1, 6));
}

static void test_lo_above_hi_means_no_range_check()
{
    TEST_ASSERT_TRUE(cmdInRange(-99999, 1, 0));
    TEST_ASSERT_TRUE(cmdInRange(99999, 1, 0));
}

static void test_an_out_of_range_value_does_not_reach_the_destination()
{
    int dest = 4;
    int seen = 0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_RANGE, cmdStoreInt("99", &dest, 1, 6, &seen));
    TEST_ASSERT_EQUAL_INT(4, dest);      // untouched
    TEST_ASSERT_EQUAL_INT(99, seen);     // but reportable
}

static void test_an_in_range_value_is_stored()
{
    int dest = 4;
    int seen = 0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreInt("5", &dest, 1, 6, &seen));
    TEST_ASSERT_EQUAL_INT(5, dest);
    TEST_ASSERT_EQUAL_INT(5, seen);
}

// `--txpower abc` end to end. The destination must survive untouched.
static void test_junk_is_rejected_not_stored()
{
    int dest = 14;
    int seen = -1;
    TEST_ASSERT_EQUAL_INT(CMD_SET_NAN, cmdStoreInt("abc", &dest, -9, 22, &seen));
    TEST_ASSERT_EQUAL_INT(14, dest);
    TEST_ASSERT_EQUAL_INT(0, seen);
}

// The trap that made rejection the right choice rather than coercion: 0 sits
// INSIDE txpower's -9..22 range, so a helper that turned junk into 0 would
// quietly store 0 dBm. NAN must outrank the range test.
static void test_junk_is_rejected_even_when_zero_would_be_in_range()
{
    int dest = 5;
    int seen = -1;
    TEST_ASSERT_EQUAL_INT(CMD_SET_NAN, cmdStoreInt("abc", &dest, 0, 10, &seen));
    TEST_ASSERT_EQUAL_INT(5, dest);
}

static void test_float_and_double_stores()
{
    float f = 1.0f;
    float fseen = 0.0f;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreFloat("0.5", &f, 0.1, 2.0, &fseen));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, f);

    TEST_ASSERT_EQUAL_INT(CMD_SET_RANGE, cmdStoreFloat("9.9", &f, 0.1, 2.0, &fseen));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, f);            // untouched
    TEST_ASSERT_EQUAL_FLOAT(9.9f, fseen);

    double d = 0.0;
    double dseen = 0.0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreDouble("2.5", &d, 1, 0, &dseen)); // no range
    TEST_ASSERT_EQUAL_DOUBLE(2.5, d);
}

static void test_null_destination_is_tolerated()
{
    int seen = 0;
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreInt("3", nullptr, 1, 6, &seen));
    TEST_ASSERT_EQUAL_INT(3, seen);
    TEST_ASSERT_EQUAL_INT(CMD_SET_OK, cmdStoreInt("3", nullptr, 1, 6, nullptr));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_plain_number_parses);
    RUN_TEST(test_a_non_numeric_argument_is_zero_not_leftover);
    RUN_TEST(test_leading_space_is_skipped);
    RUN_TEST(test_trailing_junk_still_yields_the_leading_number);
    RUN_TEST(test_percent_i_keeps_its_auto_base);
    RUN_TEST(test_range_is_inclusive);
    RUN_TEST(test_lo_above_hi_means_no_range_check);
    RUN_TEST(test_an_out_of_range_value_does_not_reach_the_destination);
    RUN_TEST(test_an_in_range_value_is_stored);
    RUN_TEST(test_junk_is_rejected_not_stored);
    RUN_TEST(test_junk_is_rejected_even_when_zero_would_be_in_range);
    RUN_TEST(test_float_and_double_stores);
    RUN_TEST(test_null_destination_is_tolerated);
    return UNITY_END();
}
