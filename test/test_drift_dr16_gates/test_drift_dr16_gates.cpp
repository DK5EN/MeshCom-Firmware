// DR-16 (docs/testplan/drift-matrix.csv): first-send gating of HEY and
// telemetry in the two main loops. Decided both-valid on 2026-10-04:
//
//   * both platforms gate the first telemetry send on their OWN flag
//     (bTeleFirst), separate from the HEY flag (bHeyFirst) -- the nRF52 bug of
//     2026-09 (telemetry tested bHeyFirst, which the HEY block clears first,
//     so the first telemetry never fired early) is the thing this test pins;
//   * bAllStarted and extra_hey_time stay ESP32-only: bAllStarted waits for
//     the asynchronous WiFi start, extra_hey_time is the softser grace. The
//     nRF52 has no WiFi state machine and the W5100S comes up synchronously
//     in setup(), so nothing is gated there -- a documented non-change.
//
// A contract test over the source text of the two gate lines: it fails when
// the nRF52 telemetry gate stops testing bTeleFirst, when either ESP32 gate
// loses its bAllStarted term, or when someone ports bAllStarted to nRF52
// without re-deciding the row.
//
//   pio test -e native -f test_drift_dr16_gates
#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

void setUp(void)    {}
void tearDown(void) {}

// pio test starts the binary with a platform-dependent cwd (see
// test_aprs_corpus.cpp openRel()).
static char *read_rel(const char *rel)
{
    static const char *prefixes[] = { "", "../", "../../", "../../../", "../../../../" };
    char path[512];
    for (const char *p : prefixes)
    {
        snprintf(path, sizeof(path), "%s%s", p, rel);
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)n + 1);
        TEST_ASSERT_NOT_NULL(buf);
        size_t got = fread(buf, 1, (size_t)n, f);
        fclose(f);
        buf[got] = '\0';
        return buf;
    }
    return nullptr;
}

// Returns the first line (as a NUL-terminated copy in `line`) that contains
// `needle`, or false.
static bool find_line(const char *buf, const char *needle, char *line, size_t linesz)
{
    const char *hit = strstr(buf, needle);
    if (!hit)
        return false;
    const char *start = hit;
    while (start > buf && start[-1] != '\n')
        start--;
    const char *end = strchr(hit, '\n');
    size_t n = end ? (size_t)(end - start) : strlen(start);
    if (n >= linesz)
        n = linesz - 1;
    memcpy(line, start, n);
    line[n] = '\0';
    return true;
}

// True when `ident` occurs on a line that is not a pure // comment line.
static bool ident_in_code(const char *buf, const char *ident)
{
    const char *p = buf;
    while ((p = strstr(p, ident)) != nullptr)
    {
        const char *start = p;
        while (start > buf && start[-1] != '\n')
            start--;
        while (*start == ' ' || *start == '\t')
            start++;
        if (!(start[0] == '/' && start[1] == '/'))
            return true;
        p += strlen(ident);
    }
    return false;
}

static char *g_esp32 = nullptr;
static char *g_nrf52 = nullptr;

static void test_sources_readable(void)
{
    g_esp32 = read_rel("src/esp32/esp32_main.cpp");
    g_nrf52 = read_rel("src/nrf52/nrf52_main.cpp");
    TEST_ASSERT_NOT_NULL_MESSAGE(g_esp32, "src/esp32/esp32_main.cpp not found");
    TEST_ASSERT_NOT_NULL_MESSAGE(g_nrf52, "src/nrf52/nrf52_main.cpp not found");
}

static void test_esp32_hey_gate_has_first_flag_and_all_started(void)
{
    char line[512];
    TEST_ASSERT_TRUE(find_line(g_esp32, "millis() - heyinfo_timer", line, sizeof(line)));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "(bHeyFirst && bAllStarted)"), line);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "extra_hey_time"), line);
}

static void test_esp32_telemetry_gate_has_own_first_flag_and_all_started(void)
{
    char line[512];
    TEST_ASSERT_TRUE(find_line(g_esp32, "millis() - telemetry_timer", line, sizeof(line)));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "(bTeleFirst && bAllStarted)"), line);
    TEST_ASSERT_NULL_MESSAGE(strstr(line, "bHeyFirst"), line);
}

static void test_nrf52_hey_gate_has_first_flag(void)
{
    char line[512];
    TEST_ASSERT_TRUE(find_line(g_nrf52, "millis() - heyinfo_timer", line, sizeof(line)));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "|| bHeyFirst"), line);
}

static void test_nrf52_telemetry_gate_tests_its_own_flag(void)
{
    char line[512];
    TEST_ASSERT_TRUE(find_line(g_nrf52, "millis() - telemetry_timer", line, sizeof(line)));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(line, "|| bTeleFirst"), line);
    TEST_ASSERT_NULL_MESSAGE(strstr(line, "bHeyFirst"), line);   // the 2026-09 bug
}

static void test_nrf52_declares_both_flags(void)
{
    TEST_ASSERT_NOT_NULL(strstr(g_nrf52, "bool bHeyFirst = true;"));
    TEST_ASSERT_NOT_NULL(strstr(g_nrf52, "bool bTeleFirst = true;"));
}

static void test_nrf52_has_no_all_started_or_extra_hey_time_in_code(void)
{
    // mentioned in the comment near bTeleFirst only, never in code
    TEST_ASSERT_FALSE_MESSAGE(ident_in_code(g_nrf52, "bAllStarted"),
                              "bAllStarted appeared in nRF52 code: re-decide DR-16 first");
    TEST_ASSERT_FALSE_MESSAGE(ident_in_code(g_nrf52, "extra_hey_time"),
                              "extra_hey_time appeared in nRF52 code: re-decide DR-16 first");
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sources_readable);
    RUN_TEST(test_esp32_hey_gate_has_first_flag_and_all_started);
    RUN_TEST(test_esp32_telemetry_gate_has_own_first_flag_and_all_started);
    RUN_TEST(test_nrf52_hey_gate_has_first_flag);
    RUN_TEST(test_nrf52_telemetry_gate_tests_its_own_flag);
    RUN_TEST(test_nrf52_declares_both_flags);
    RUN_TEST(test_nrf52_has_no_all_started_or_extra_hey_time_in_code);
    free(g_esp32);
    free(g_nrf52);
    return UNITY_END();
}
