// Native test for the /pos.dat position-field parser of the T-Deck (TD-15).
//
// After a reboot the map was empty: loadPosPersistence() restored the POS table
// but not the map marker arrays. The restore parses the 24-byte position field
// that tdeck_add_to_pos_view() wrote with "%.2lf%c/%.2lf%c/%i" (magnitudes plus
// hemisphere letters). The parser lives in the pure header src/t-deck/pos_persist.h.
//
//   pio test -e native -f test_pos_persist

#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include <t-deck/pos_persist.h>

void setUp(void)    {}
void tearDown(void) {}

// The exact writer format of tdeck_add_to_pos_view(), buffer of 24 as there.
static void make_field(char *buf, double lat, char lat_c, double lon, char lon_c, int alt)
{
    snprintf(buf, 24, "%.2lf%c/%.2lf%c/%i", lat, lat_c, lon, lon_c, alt);
}

static void test_round_trip_north_east(void)
{
    char buf[24];
    make_field(buf, 48.21, 'N', 16.37, 'E', 183);

    double lat = 0, lon = 0; char lat_c = 0, lon_c = 0; int alt = -1;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field(buf, lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 48.21, lat);
    TEST_ASSERT_EQUAL_CHAR('N', lat_c);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 16.37, lon);
    TEST_ASSERT_EQUAL_CHAR('E', lon_c);
    TEST_ASSERT_EQUAL_INT(183, alt);
}

// A leading minus (a hand-edited or future signed row) is dropped; the sign
// still comes from the hemisphere letter only.
static void test_leading_minus_is_dropped(void)
{
    double lat = 0, lon = 0; char lat_c = 0, lon_c = 0; int alt = -1;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field("-33.87S/-151.21W/5", lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 33.87, lat);
    TEST_ASSERT_EQUAL_CHAR('S', lat_c);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 151.21, lon);
    TEST_ASSERT_EQUAL_CHAR('W', lon_c);
    TEST_ASSERT_EQUAL_INT(5, alt);
}

static void test_round_trip_south_west(void)
{
    char buf[24];
    make_field(buf, 33.87, 'S', 151.21, 'W', 5);

    double lat = 0, lon = 0; char lat_c = 0, lon_c = 0; int alt = -1;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field(buf, lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 33.87, lat);
    TEST_ASSERT_EQUAL_CHAR('S', lat_c);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 151.21, lon);
    TEST_ASSERT_EQUAL_CHAR('W', lon_c);
    TEST_ASSERT_EQUAL_INT(5, alt);
}

// The hemisphere letter E right after the longitude must not be read as an
// exponent ("16.37E/200" is not 16.37e/200).
static void test_east_letter_is_not_an_exponent(void)
{
    double lat = 0, lon = 0; char lat_c = 0, lon_c = 0; int alt = -1;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field("1.00N/2.00E/3", lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0, lon);
    TEST_ASSERT_EQUAL_CHAR('E', lon_c);
    TEST_ASSERT_EQUAL_INT(3, alt);
}

static void test_negative_altitude(void)
{
    double lat = 0, lon = 0; char lat_c = 0, lon_c = 0; int alt = 0;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field("31.50N/35.50E/-396", lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_EQUAL_INT(-396, alt);
}

// 24-byte on-disk field, space padded (savePosPersistence "%-24.24s"), read back
// as-is: the padding must not matter.
static void test_padded_24_byte_field(void)
{
    char buf[24];
    make_field(buf, 47.07, 'N', 15.44, 'E', 353);

    char padded[25];
    snprintf(padded, sizeof(padded), "%-24.24s", buf);
    TEST_ASSERT_EQUAL_INT(24, (int)strlen(padded));

    double lat = 0, lon = 0; char lat_c = 0, lon_c = 0; int alt = -1;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field(padded, lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 47.07, lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 15.44, lon);
    TEST_ASSERT_EQUAL_INT(353, alt);
}

// A short or trimmed field without altitude still gives a map point.
static void test_short_field_without_altitude(void)
{
    double lat = 0, lon = 0; char lat_c = 0, lon_c = 0; int alt = -1;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field("48.21N/16.37E", lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 48.21, lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 16.37, lon);
    TEST_ASSERT_EQUAL_INT(0, alt);

    alt = -1;
    TEST_ASSERT_TRUE(tdeck_parse_pos_field("48.21N/16.37E/", lat, lat_c, lon, lon_c, alt));
    TEST_ASSERT_EQUAL_INT(0, alt);
}

static void test_garbage_returns_false(void)
{
    double lat = 7.0, lon = 8.0; char lat_c = 'x', lon_c = 'y'; int alt = 9;

    const char *bad[] = {
        "", "   ", "garbage", "N/E", "48.21N", "48.21N/", "48.21N/E", "48.21X/16.37E/1",
        "48.21N/16.37X/1", "48.21N 16.37E 1", "N48.21/E16.37/1", ".N/.E/1",
        "91.00N/16.37E/1", "48.21N/181.00E/1", "-/-", "/",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(tdeck_parse_pos_field(bad[i], lat, lat_c, lon, lon_c, alt), bad[i]);

    TEST_ASSERT_FALSE(tdeck_parse_pos_field(nullptr, lat, lat_c, lon, lon_c, alt));

    // a failed parse leaves the outputs untouched
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 7.0, lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 8.0, lon);
    TEST_ASSERT_EQUAL_CHAR('x', lat_c);
    TEST_ASSERT_EQUAL_CHAR('y', lon_c);
    TEST_ASSERT_EQUAL_INT(9, alt);
}

static void test_callsign_trim(void)
{
    char out[11];

    // "%-10.10s" as written by savePosPersistence()
    char raw[16];
    snprintf(raw, sizeof(raw), "%-10.10s", "DK5EN-14");
    tdeck_pos_trim(raw, 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("DK5EN-14", out);

    snprintf(raw, sizeof(raw), "%-10.10s", "OE3GJC-12");
    tdeck_pos_trim(raw, 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("OE3GJC-12", out);

    // full 10 bytes, no padding
    tdeck_pos_trim("ABCDEFGHIJ", 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJ", out);

    // only the first len bytes count
    tdeck_pos_trim("ABCDEFGHIJKLM", 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHIJ", out);

    // leading blanks, empty and all-blank rows
    tdeck_pos_trim("  DL1ABC  ", 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("DL1ABC", out);
    tdeck_pos_trim("          ", 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);

    // NUL inside the record ends it; a small output buffer truncates, not overruns
    tdeck_pos_trim("AB\0CD     ", 10, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AB", out);
    char small[4];
    tdeck_pos_trim("DK5EN-14", 8, small, sizeof(small));
    TEST_ASSERT_EQUAL_STRING("DK5", small);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_north_east);
    RUN_TEST(test_round_trip_south_west);
    RUN_TEST(test_leading_minus_is_dropped);
    RUN_TEST(test_east_letter_is_not_an_exponent);
    RUN_TEST(test_negative_altitude);
    RUN_TEST(test_padded_24_byte_field);
    RUN_TEST(test_short_field_without_altitude);
    RUN_TEST(test_garbage_returns_false);
    RUN_TEST(test_callsign_trim);
    return UNITY_END();
}
