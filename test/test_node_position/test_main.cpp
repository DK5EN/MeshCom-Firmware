// DRY-01 -- src/node_position.h: hemisphere split, range check and signed decode of the own position.
// The three setters (serial --setlat/--setlon, BLE 0x70/0x80, RM "pos") share this logic through
// nodeSetPosition() in command_functions.cpp; that file has no host env, so the pure part is pinned here.
#include <unity.h>

#include <math.h>
#include <stdint.h>

#include <node_position.h>

// Unity double asserts are compiled out on this env: compare exactly through TEST_ASSERT_TRUE.
#define ASSERT_DBL(e, a) TEST_ASSERT_TRUE((double)(e) == (double)(a))

void setUp(void) {}
void tearDown(void) {}

static void splitRoundTrip(double lat, double lon, char eLatHem, char eLonHem)
{
    double aLat = -1, aLon = -1;
    char hLat = '?', hLon = '?';
    TEST_ASSERT_TRUE(nodePosSplit(lat, lon, aLat, hLat, aLon, hLon));
    TEST_ASSERT_EQUAL_CHAR(eLatHem, hLat);
    TEST_ASSERT_EQUAL_CHAR(eLonHem, hLon);
    TEST_ASSERT_TRUE(aLat >= 0.0);
    TEST_ASSERT_TRUE(aLon >= 0.0);
    ASSERT_DBL(fabs(lat), aLat);
    ASSERT_DBL(fabs(lon), aLon);
    ASSERT_DBL(lat, nodeSignedLat(aLat, hLat));
    ASSERT_DBL(lon, nodeSignedLon(aLon, hLon));
}

static void test_four_hemispheres(void)
{
    splitRoundTrip(48.12345, 16.54321, 'N', 'E');
    splitRoundTrip(-33.86882, 151.20929, 'S', 'E');
    splitRoundTrip(40.71278, -74.00594, 'N', 'W');
    splitRoundTrip(-34.60368, -58.38156, 'S', 'W');
}

static void test_boundaries_accepted(void)
{
    splitRoundTrip(90.0, 180.0, 'N', 'E');
    splitRoundTrip(-90.0, -180.0, 'S', 'W');
    splitRoundTrip(90.0, -180.0, 'N', 'W');
    splitRoundTrip(-90.0, 180.0, 'S', 'E');
}

static void test_out_of_range_rejected(void)
{
    double aLat = 7, aLon = 7;
    char hLat = 'x', hLon = 'y';
    const double badLat[] = {90.00001, -90.00001, 95.0, -95.0, 1000.0, NAN, INFINITY, -INFINITY};
    const double badLon[] = {180.00001, -180.00001, 181.0, -200.0, 1e9, NAN, INFINITY, -INFINITY};
    for (double v : badLat)
    {
        TEST_ASSERT_FALSE(nodeLatValid(v));
        TEST_ASSERT_FALSE(nodePosSplit(v, 10.0, aLat, hLat, aLon, hLon));
        TEST_ASSERT_FALSE(nodeLatSplit(v, aLat, hLat));
    }
    for (double v : badLon)
    {
        TEST_ASSERT_FALSE(nodeLonValid(v));
        TEST_ASSERT_FALSE(nodePosSplit(10.0, v, aLat, hLat, aLon, hLon));
        TEST_ASSERT_FALSE(nodeLonSplit(v, aLon, hLon));
    }
    // a reject leaves every output untouched (no half-written position)
    ASSERT_DBL(7, aLat);
    ASSERT_DBL(7, aLon);
    TEST_ASSERT_EQUAL_CHAR('x', hLat);
    TEST_ASSERT_EQUAL_CHAR('y', hLon);
}

// Pre-fix divergence: serial --setlat 95 and a BLE latitude of 95 used to be stored as 95 N (only RM
// range-checked). The shared helper refuses it, so a version without the range check fails this test.
static void test_serial_ble_lat95_now_rejected(void)
{
    double mag = 0;
    char hem = 'N';
    TEST_ASSERT_FALSE(nodeLatSplit(95.0, mag, hem));
    TEST_ASSERT_FALSE(nodeLonSplit(181.0, mag, hem));
    double aLat, aLon;
    char hLat, hLon;
    TEST_ASSERT_FALSE(nodePosSplit(95.0, 10.0, aLat, hLat, aLon, hLon));
}

static void test_zero_handling(void)
{
    double mag = -1;
    char hem = '?';
    TEST_ASSERT_TRUE(nodeLatSplit(0.0, mag, hem));
    TEST_ASSERT_EQUAL_CHAR('N', hem);
    ASSERT_DBL(0.0, mag);

    // -0.0 is not "< 0": north, and the stored magnitude is +0 (sign bit cleared)
    mag = -1;
    TEST_ASSERT_TRUE(nodeLatSplit(-0.0, mag, hem));
    TEST_ASSERT_EQUAL_CHAR('N', hem);
    ASSERT_DBL(0.0, mag);
    TEST_ASSERT_FALSE(signbit(mag));

    mag = -1;
    TEST_ASSERT_TRUE(nodeLonSplit(-0.0, mag, hem));
    TEST_ASSERT_EQUAL_CHAR('E', hem);
    TEST_ASSERT_FALSE(signbit(mag));

    // a tiny negative value is south / west
    TEST_ASSERT_TRUE(nodeLatSplit(-0.00001, mag, hem));
    TEST_ASSERT_EQUAL_CHAR('S', hem);
    TEST_ASSERT_TRUE(nodeLonSplit(-0.00001, mag, hem));
    TEST_ASSERT_EQUAL_CHAR('W', hem);

    // 0/0 stays "position unset" after the round trip
    double aLat, aLon;
    char hLat, hLon;
    TEST_ASSERT_TRUE(nodePosSplit(0.0, 0.0, aLat, hLat, aLon, hLon));
    TEST_ASSERT_TRUE(aLat == 0.0 && aLon == 0.0);
}

static void test_signed_decode_conventions(void)
{
    ASSERT_DBL(-12.5, nodeSignedLat(12.5, 'S'));
    ASSERT_DBL(12.5, nodeSignedLat(12.5, 'N'));
    ASSERT_DBL(-12.5, nodeSignedLon(12.5, 'W'));
    ASSERT_DBL(12.5, nodeSignedLon(12.5, 'E'));
    // an unset hemisphere letter (fresh settings: ' ') decodes positive, as the old copies did
    ASSERT_DBL(12.5, nodeSignedLat(12.5, ' '));
    ASSERT_DBL(12.5, nodeSignedLon(12.5, ' '));
    // each axis decodes only its own letters
    ASSERT_DBL(12.5, nodeSignedLat(12.5, 'W'));
    ASSERT_DBL(12.5, nodeSignedLon(12.5, 'S'));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_four_hemispheres);
    RUN_TEST(test_boundaries_accepted);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_serial_ble_lat95_now_rejected);
    RUN_TEST(test_zero_handling);
    RUN_TEST(test_signed_decode_conventions);
    return UNITY_END();
}
