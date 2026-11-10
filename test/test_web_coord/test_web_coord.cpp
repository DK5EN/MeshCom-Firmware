// Host-Test fuer src/web_functions/web_coord.h -- P-0124: setlat/setlon-Read-back.
//
//   pio test -e native_web_coord -f test_web_coord
//
// Faelle: Werte, die als float nicht exakt sind (49.997), Vorzeichen als Betrag,
// Aenderung erkannt, und die Genauigkeit von 1e-7 Grad.

#include <unity.h>

#include <web_functions/web_coord.h>

void setUp(void) {}
void tearDown(void) {}

// Der Fehler: 49.997 ist als float nicht exakt. Ein float-Vergleich haette hier FAIL geliefert.
void test_non_float_exact_latitude_matches(void)
{
    TEST_ASSERT_TRUE(webCoordMatches(49.997, "49.997"));
}

void test_float_exact_latitude_matches(void)
{
    TEST_ASSERT_TRUE(webCoordMatches(48.5, "48.5"));
}

// --setlat/--setlon speichern Sued und West als Betrag, der Vergleich nimmt den Betrag.
void test_south_and_west_compare_by_magnitude(void)
{
    TEST_ASSERT_TRUE(webCoordMatches(49.997, "-49.997"));
    TEST_ASSERT_TRUE(webCoordMatches(16.414656, "-16.414656"));
}

void test_changed_value_is_detected(void)
{
    TEST_ASSERT_FALSE(webCoordMatches(49.997, "49.998"));
}

void test_precision_is_one_ten_millionth_degree(void)
{
    TEST_ASSERT_TRUE(webCoordMatches(16.40922749, "16.40922749"));
    TEST_ASSERT_FALSE(webCoordMatches(16.409227, "16.40922749"));   // 4.9e-7 Grad Abweichung
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_non_float_exact_latitude_matches);
    RUN_TEST(test_float_exact_latitude_matches);
    RUN_TEST(test_south_and_west_compare_by_magnitude);
    RUN_TEST(test_changed_value_is_detected);
    RUN_TEST(test_precision_is_one_ten_millionth_degree);
    return UNITY_END();
}
