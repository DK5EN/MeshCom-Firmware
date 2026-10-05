// Native testsuite for SafebootVerScan -- the streaming scanner that reads the Safeboot capability
// version out of the Safeboot partition (src/safeboot/safeboot_ver.h, campaign #1187 AU-12).
// Marker string "MCSB;ver;NNN" -> N; legacy fallback FWS2+fwstage+nocap -> 1; otherwise 0.
//
//   pio test -e native_safeboot -f test_safeboot_ver

#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include <safeboot/safeboot_ver.h>

void setUp(void) {}
void tearDown(void) {}

static int scanWhole(const std::string &s) {
    SafebootVerScan sc;
    sc.feed((const uint8_t *)s.data(), s.size());
    return sc.version();
}

static int scanSplit(const std::string &s, size_t at) {
    SafebootVerScan sc;
    sc.feed((const uint8_t *)s.data(), at);
    sc.feed((const uint8_t *)s.data() + at, s.size() - at);
    return sc.version();
}

static int scanBytewise(const std::string &s) {
    SafebootVerScan sc;
    for (size_t i = 0; i < s.size(); i++)
        sc.feed((const uint8_t *)s.data() + i, 1);
    return sc.version();
}

// Checks whole / every split offset / byte-by-byte agree on `expected`.
static void expectAll(const std::string &s, int expected) {
    TEST_ASSERT_EQUAL_INT(expected, scanWhole(s));
    TEST_ASSERT_EQUAL_INT(expected, scanBytewise(s));
    for (size_t at = 0; at <= s.size(); at++)
        TEST_ASSERT_EQUAL_INT_MESSAGE(expected, scanSplit(s, at), "split offset differs");
}

static const std::string kLegacy = std::string("xx FWS2 yy") + '\0' + "fwstage" + '\0' + "nocap zz";

static void test_empty_buffer_is_zero(void) {
    SafebootVerScan sc;
    TEST_ASSERT_EQUAL_INT(0, sc.version());
    sc.feed(nullptr, 0);
    TEST_ASSERT_EQUAL_INT(0, sc.version());
    TEST_ASSERT_EQUAL_INT(0, scanWhole(""));
}

static void test_marker_alone(void) {
    expectAll("MCSB;ver;002", 2);
}

static void test_marker_with_surrounding_garbage(void) {
    std::string s = std::string("garbage MCSB;ver;x MM") + '\0' + '\xff' + "MCSB;ver;003" + '\0' + "tail MCSB";
    // first complete marker wins: the "MCSB;ver;x" prefix is not a valid marker, the next one is
    expectAll(s, 3);
    expectAll(std::string(100, 'A') + "MCSB;ver;002" + std::string(100, 'B'), 2);
}

static void test_marker_ten(void) {
    expectAll("MCSB;ver;010", 10);
    expectAll("pre MCSB;ver;999 post", 999);
}

static void test_invalid_markers_are_ignored(void) {
    expectAll("MCSB;ver;000", 0);
    expectAll("MCSB;ver;0a2", 0);
    expectAll("MCSB;ver;0", 0);   // truncated at end of buffer
    expectAll("MCSB;ver;00", 0);  // truncated at end of buffer
    expectAll("MCSB;ver;", 0);
}

static void test_invalid_marker_falls_back_to_string_rule(void) {
    expectAll(kLegacy + " MCSB;ver;0", 1);
    expectAll(kLegacy + " MCSB;ver;000", 1);
    expectAll("MCSB;ver;0a2 " + kLegacy, 1);
}

static void test_legacy_all_three_is_one(void) {
    expectAll(kLegacy, 1);
}

static void test_legacy_any_two_is_zero(void) {
    expectAll("FWS2 fwstage", 0);
    expectAll("FWS2 nocap", 0);
    expectAll("fwstage nocap", 0);
    expectAll("FWS2", 0);
    expectAll("fwstage", 0);
    expectAll("nocap", 0);
}

static void test_marker_wins_over_legacy(void) {
    expectAll(kLegacy + " MCSB;ver;004", 4);
    expectAll("MCSB;ver;005 " + kLegacy, 5);
}

static void test_legacy_strings_split_across_chunks(void) {
    // exercise the carry for the shorter patterns too (every offset, bytewise): covered by expectAll
    expectAll(std::string("..") + "fwstage" + ".." + "nocap" + ".." + "FWS2", 1);
}

static void test_reset_clears_state(void) {
    SafebootVerScan sc;
    const std::string s = "MCSB;ver;007";
    sc.feed((const uint8_t *)s.data(), s.size());
    TEST_ASSERT_EQUAL_INT(7, sc.version());
    sc.reset();
    TEST_ASSERT_EQUAL_INT(0, sc.version());
}

static void test_macro_marker_parses_to_safeboot_version(void) {
    TEST_ASSERT_EQUAL_INT(SAFEBOOT_VERSION, scanWhole(SAFEBOOT_MARKER));
    TEST_ASSERT_EQUAL_size_t(SAFEBOOT_MARKER_PREFIX_LEN, strlen(SAFEBOOT_MARKER_PREFIX));
    TEST_ASSERT_EQUAL_size_t(SAFEBOOT_MARKER_PREFIX_LEN + SAFEBOOT_MARKER_DIGITS, strlen(SAFEBOOT_MARKER));
    TEST_ASSERT_EQUAL_INT(0, strncmp(SAFEBOOT_MARKER, SAFEBOOT_MARKER_PREFIX, SAFEBOOT_MARKER_PREFIX_LEN));
}

static void test_min_not_above_current(void) {
    TEST_ASSERT_TRUE(AU_SAFEBOOT_MIN <= SAFEBOOT_VERSION);
    TEST_ASSERT_TRUE(AU_SAFEBOOT_MIN >= 1);
}

// ---------------------------------------------------------------------
// Repo files. pio test starts the binary with differing cwd, so try a few prefixes.
// ---------------------------------------------------------------------
static FILE *openRel(const char *rel) {
    static const char *prefixes[] = {"", "../", "../../", "../../../", "../../../../"};
    char path[512];
    for (const char *p : prefixes) {
        snprintf(path, sizeof(path), "%s%s", p, rel);
        FILE *f = fopen(path, "rb");
        if (f)
            return f;
    }
    return nullptr;
}

static void scanRepoImage(const char *rel, int *outVersion) {
    FILE *f = openRel(rel);
    TEST_ASSERT_NOT_NULL_MESSAGE(f, rel);
    SafebootVerScan sc;
    uint8_t buf[4096];
    size_t total = 0, n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        sc.feed(buf, n);
        total += n;
    }
    fclose(f);
    TEST_ASSERT_GREATER_THAN_size_t_MESSAGE(0, total, rel);
    *outVersion = sc.version();
}

static void test_repo_safeboot_bin_is_au_capable(void) {
    int v = -1;
    scanRepoImage("safeboot.bin", &v);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SAFEBOOT_VERSION, v, "committed Safeboot image is not rebuilt for SAFEBOOT_VERSION");
}

static void test_repo_safeboot_s3_bin_is_au_capable(void) {
    int v = -1;
    scanRepoImage("safeboot-s3.bin", &v);
    TEST_ASSERT_EQUAL_INT_MESSAGE(SAFEBOOT_VERSION, v, "committed Safeboot image is not rebuilt for SAFEBOOT_VERSION");
}

static size_t countOccurrences(const std::string &hay, const char *needle) {
    size_t cnt = 0, nl = strlen(needle), pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        cnt++;
        pos += nl;
    }
    return cnt;
}

static void test_main_cpp_marker_comes_only_from_macro(void) {
    FILE *f = openRel("src/safeboot/main.cpp");
    TEST_ASSERT_NOT_NULL(f);
    std::string src;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        src.append(buf, n);
    fclose(f);
    TEST_ASSERT_GREATER_THAN_size_t(0, src.size());
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, countOccurrences(src, SAFEBOOT_MARKER_PREFIX),
                                     "main.cpp must not spell the marker prefix; it comes from SAFEBOOT_MARKER");
    TEST_ASSERT_EQUAL_size_t(1, countOccurrences(src, "kSafebootMarker"));
    TEST_ASSERT_EQUAL_size_t(1, countOccurrences(src, "= SAFEBOOT_MARKER;"));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_empty_buffer_is_zero);
    RUN_TEST(test_marker_alone);
    RUN_TEST(test_marker_with_surrounding_garbage);
    RUN_TEST(test_marker_ten);
    RUN_TEST(test_invalid_markers_are_ignored);
    RUN_TEST(test_invalid_marker_falls_back_to_string_rule);
    RUN_TEST(test_legacy_all_three_is_one);
    RUN_TEST(test_legacy_any_two_is_zero);
    RUN_TEST(test_marker_wins_over_legacy);
    RUN_TEST(test_legacy_strings_split_across_chunks);
    RUN_TEST(test_reset_clears_state);
    RUN_TEST(test_macro_marker_parses_to_safeboot_version);
    RUN_TEST(test_min_not_above_current);
    RUN_TEST(test_repo_safeboot_bin_is_au_capable);
    RUN_TEST(test_repo_safeboot_s3_bin_is_au_capable);
    RUN_TEST(test_main_cpp_marker_comes_only_from_macro);
    return UNITY_END();
}
