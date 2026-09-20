// Twin test for countryProfile() (test plan U6, audit row D3-05).
//
// This is the first of the N1 characterization tests, and it is possible at
// all because the C5 carve-out moved the country table into its own
// translation unit that depends on nothing but the per-variant configuration
// macros -- no meshcom_settings, no radio globals.
//
// "Twin" here means built twice rather than run twice: the two platform sides
// differ only in those macros, so each native env supplies one variant's
// header (test/test_country_twin/stubs_esp32 and stubs_nrf52) and, for the
// nRF52 side, -D BOARD_RAK4630.
//
//   pio test -e native_country_esp32 -f test_country_twin
//   pio test -e native_country_nrf52 -f test_country_twin
//
// The pass condition is NOT "the two sides agree" -- they must not, the
// values are in different units (OPT-D14): MHz against Hz, kHz against a
// bandwidth index, a 4/N denominator against a coding-rate index. The pass
// condition is that each side still produces the table it produced before the
// unification, so the committed files under test/golden/native/ are the
// evidence, not this source.
//
// The table is also printed on every run, so regenerating a committed file
// after a deliberate change is a copy-paste from the test output rather than
// a hand edit.

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <country_profile.h>

#if defined(BOARD_RAK4630) || defined(USE_HELTEC_T114) || defined(BOARD_T_ECHO)
#define SIDE "nrf52"
#else
#define SIDE "esp32"
#endif

#define EXPECTED_PATH "test/golden/native/country-profile-" SIDE ".txt"

// 1..15 is the range --setctry accepts; 0 and 16 are there because the switch
// has a `default` and someone will eventually ask what it does with them.
static const int CODES[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                            10, 11, 12, 13, 14, 15, 16};
static const int N_CODES = sizeof(CODES) / sizeof(CODES[0]);

void setUp(void) {}
void tearDown(void) {}

// One line per code, in the committed files' format. Country 7 is the one
// code that is not a table entry -- it validates what is already stored
// rather than assigning literals -- so it has no tuple to print.
static void format_row(int code, char *out, size_t n)
{
    CountryProfile p;
    memset(&p, 0, sizeof(p));
    if (!countryProfile(code, p))
    {
        snprintf(out, n, "%2d  MANUAL (not a table entry)", code);
        return;
    }
    snprintf(out, n, "%2d  freq=%.6f bw=%.1f sf=%d cr=%d track=%.6f preamble=%d",
             code, (double)p.freq, (double)p.bw, p.sf, p.cr,
             (double)p.track_freq, p.preamble);
}

static void test_country_7_is_not_a_table_entry(void)
{
    // The C5 carve lifted country 7 out of the switch, which left `default`
    // free to catch it and hand back the EU profile -- manual mode would have
    // silently become EU. This is that trap, pinned.
    CountryProfile p;
    memset(&p, 0, sizeof(p));
    TEST_ASSERT_FALSE(countryProfile(7, p));
}

static void test_every_other_code_is_a_table_entry(void)
{
    for (int i = 0; i < N_CODES; i++)
    {
        if (CODES[i] == 7)
            continue;
        CountryProfile p;
        memset(&p, 0, sizeof(p));
        char msg[64];
        snprintf(msg, sizeof(msg), "code %d", CODES[i]);
        TEST_ASSERT_TRUE_MESSAGE(countryProfile(CODES[i], p), msg);
    }
}

static void test_unknown_codes_fall_to_the_eu_default(void)
{
    // 0, 3 and 16 have no case; the switch's `default` is EU. Worth pinning
    // because "unknown country code" is exactly what a corrupt settings blob
    // produces.
    CountryProfile eu, zero, three, sixteen;
    memset(&eu, 0, sizeof(eu));
    memset(&zero, 0, sizeof(zero));
    memset(&three, 0, sizeof(three));
    memset(&sixteen, 0, sizeof(sixteen));
    countryProfile(99, eu);
    countryProfile(0, zero);
    countryProfile(3, three);
    countryProfile(16, sixteen);
    TEST_ASSERT_EQUAL_MEMORY(&eu, &zero, sizeof(eu));
    TEST_ASSERT_EQUAL_MEMORY(&eu, &three, sizeof(eu));
    TEST_ASSERT_EQUAL_MEMORY(&eu, &sixteen, sizeof(eu));
}

static void test_table_matches_the_committed_baseline(void)
{
    printf("\n--- countryProfile(), " SIDE " side ---\n");
    char rows[N_CODES][128];
    for (int i = 0; i < N_CODES; i++)
    {
        format_row(CODES[i], rows[i], sizeof(rows[i]));
        printf("%s\n", rows[i]);
    }
    printf("--- end, baseline: " EXPECTED_PATH " ---\n\n");

    FILE *f = fopen(EXPECTED_PATH, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(
        f, "no committed baseline at " EXPECTED_PATH
           " -- copy the table printed above into it");

    char line[128];
    int i = 0;
    while (fgets(line, sizeof(line), f) && i < N_CODES)
    {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0 || line[0] == '#')
            continue;
        char msg[256];
        snprintf(msg, sizeof(msg), "row %d: baseline %s", i, line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(line, rows[i], msg);
        i++;
    }
    fclose(f);
    TEST_ASSERT_EQUAL_INT_MESSAGE(N_CODES, i,
                                  "baseline has a different number of rows");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_country_7_is_not_a_table_entry);
    RUN_TEST(test_every_other_code_is_a_table_entry);
    RUN_TEST(test_unknown_codes_fall_to_the_eu_default);
    RUN_TEST(test_table_matches_the_committed_baseline);
    return UNITY_END();
}
