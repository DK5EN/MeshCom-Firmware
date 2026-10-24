// BLK-01 -- call blocklist: validation, normalisation, parsing, matching.
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "blocked_calls.h"

static char s_dropped[8][24];
static int s_ndropped;

static void captureDrop(const char *e)
{
    if (s_ndropped < 8)
        snprintf(s_dropped[s_ndropped], sizeof(s_dropped[0]), "%s", e);
    s_ndropped++;
}

void setUp(void)
{
    blockListSet("none");
    s_ndropped = 0;
}
void tearDown(void) {}

static void expectNorm(const char *in, const char *want)
{
    char out[BLOCKLIST_CALL_MAX + 1];
    TEST_ASSERT_TRUE_MESSAGE(blockCallNormalize(in, out, sizeof(out)), in);
    TEST_ASSERT_EQUAL_STRING(want, out);
}

static void expectInvalid(const char *in)
{
    char out[BLOCKLIST_CALL_MAX + 1];
    TEST_ASSERT_FALSE_MESSAGE(blockCallNormalize(in, out, sizeof(out)), in);
}

static void test_valid_calls(void)
{
    expectNorm("DK5EN", "DK5EN");
    expectNorm("DK5EN-12", "DK5EN-12");
    expectNorm("DL1ABC-9", "DL1ABC-9");
    expectNorm("DE1A", "DE1A");        // 4-char base with letter and digit is fine
    expectNorm("9A1", "9A1");          // 3 chars is the minimum
    expectNorm("ABCDE1-15", "ABCDE1-15");   // 9 chars, the maximum
    expectNorm("  DK5EN  ", "DK5EN");
}

static void test_invalid_calls(void)
{
    expectInvalid("");
    expectInvalid("AB");
    expectInvalid("A1");
    expectInvalid("ABCDEFG1");
    expectInvalid("ABCDEF");
    expectInvalid("123456");
    expectInvalid("DK5EN-");
    expectInvalid("DK5EN-123");
    expectInvalid("DK5EN-1A");
    expectInvalid("DK5EN--1");
    expectInvalid("DK5EN-A");
    expectInvalid("DK5 EN");
    expectInvalid("DK5E*");
    expectInvalid("-1");
    expectInvalid("*");
    expectInvalid("TEST");
    expectInvalid("WLNK-1");
}

static void test_lower_case(void)
{
    expectNorm("dk5en", "DK5EN");
    expectNorm("dk5en-3", "DK5EN-3");
}

static void test_ssid_normalisation(void)
{
    expectNorm("DK5EN-01", "DK5EN-1");
    expectNorm("DK5EN-0", "DK5EN");
    expectNorm("DK5EN-00", "DK5EN");
    expectNorm("DK5EN-10", "DK5EN-10");
}

static void test_small_out_buffer_rejected(void)
{
    char out[4];
    TEST_ASSERT_FALSE(blockCallNormalize("DK5EN", out, sizeof(out)));
}

static void expectParse(const char *in, const char *want)
{
    char out[BLOCKLIST_CSV_MAX], bad[24];
    TEST_ASSERT_TRUE_MESSAGE(blockListParse(in, out, sizeof(out), bad, sizeof(bad)), in);
    TEST_ASSERT_EQUAL_STRING(want, out);
}

static void test_parse_separators_and_spaces(void)
{
    expectParse("dk5en, dl1abc-01 ;  DL1AB-2", "DK5EN,DL1ABC-1,DL1AB-2");
    expectParse(" DK5EN ", "DK5EN");
}

static void test_parse_empty_tokens(void)
{
    expectParse(",,DK5EN,;, ,DL1ABC,", "DK5EN,DL1ABC");
}

static void test_parse_duplicates(void)
{
    expectParse("DK5EN-01,dk5en-1", "DK5EN-1");
    expectParse("DK5EN,DL1ABC,DK5EN-0,DL1ABC", "DK5EN,DL1ABC");
    // bare and SSID entries are different entries
    expectParse("DK5EN,DK5EN-1", "DK5EN,DK5EN-1");
}

static void test_parse_none(void)
{
    expectParse("none", "");
    expectParse("NONE", "");
    expectParse("  None  ", "");
    expectParse("", "");
    expectParse("   ", "");
}

static void test_parse_limit(void)
{
    char in[400] = "";
    char out[BLOCKLIST_CSV_MAX], bad[24];
    // 16 entries of the maximum length still fit the CSV buffer
    for (int i = 0; i < 16; i++)
        snprintf(in + strlen(in), sizeof(in) - strlen(in), "%sABCD%02d-15", i ? "," : "", i);
    TEST_ASSERT_TRUE(blockListParse(in, out, sizeof(out), bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_UINT(BLOCKLIST_CSV_MAX - 1, strlen(out));

    snprintf(in + strlen(in), sizeof(in) - strlen(in), ",QQ1QQ");
    TEST_ASSERT_FALSE(blockListParse(in, out, sizeof(out), bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("too many", bad);
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_parse_duplicate_does_not_count_toward_limit(void)
{
    char in[400] = "";
    for (int i = 0; i < 20; i++)
        snprintf(in + strlen(in), sizeof(in) - strlen(in), "AB%dXY,", i % 10);
    // 10 distinct, 10 duplicates
    char out[BLOCKLIST_CSV_MAX], bad[24];
    TEST_ASSERT_TRUE(blockListParse(in, out, sizeof(out), bad, sizeof(bad)));
}

// A token longer than the scan buffer must not clip into a valid call.
static void test_overlong_token_not_clipped_valid(void)
{
    char out[BLOCKLIST_CSV_MAX], bad[32];
    TEST_ASSERT_FALSE(blockListParse("DK5EN                              X", out, sizeof(out), bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_parse_reports_bad_token(void)
{
    char out[BLOCKLIST_CSV_MAX], bad[24];
    TEST_ASSERT_FALSE(blockListParse("DK5EN, dk5en-123 ,DL1ABC", out, sizeof(out), bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("dk5en-123", bad);
    TEST_ASSERT_EQUAL_STRING("", out);

    // a clipped bad buffer is terminated
    char tiny[4];
    TEST_ASSERT_FALSE(blockListParse("NOTACALLSIGNATALL", out, sizeof(out), tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL_STRING("NOT", tiny);
}

static void test_set_failure_keeps_old_list(void)
{
    char bad[24], got[BLOCKLIST_CSV_MAX];
    TEST_ASSERT_TRUE(blockListSet("dk5en,DL1ABC-1", bad, sizeof(bad)));
    TEST_ASSERT_FALSE(blockListSet("DL1AB,XX", bad, sizeof(bad)));
    TEST_ASSERT_EQUAL_STRING("XX", bad);
    blockListGet(got, sizeof(got));
    TEST_ASSERT_EQUAL_STRING("DK5EN,DL1ABC-1", got);
    TEST_ASSERT_EQUAL_UINT8(2, blockListCount());
    TEST_ASSERT_FALSE(blockListSet("DL1AB,XX"));   // default args
}

static void test_set_none_clears(void)
{
    blockListSet("DK5EN");
    TEST_ASSERT_EQUAL_UINT8(1, blockListCount());
    TEST_ASSERT_TRUE(blockListSet("none"));
    TEST_ASSERT_EQUAL_UINT8(0, blockListCount());
    TEST_ASSERT_FALSE(isBlockedCall("DK5EN"));
}

static void test_empty_list_blocks_nothing(void)
{
    TEST_ASSERT_FALSE(isBlockedCall("DK5EN"));
    TEST_ASSERT_FALSE(isBlockedInPath("DK5EN,DL1ABC*"));
    TEST_ASSERT_FALSE(isBlockedFrame("DK5EN", "DL1AB"));
}

static void test_bare_entry_blocks_any_ssid(void)
{
    blockListSet("DK5EN");
    TEST_ASSERT_TRUE(isBlockedCall("DK5EN"));
    TEST_ASSERT_TRUE(isBlockedCall("DK5EN-1"));
    TEST_ASSERT_TRUE(isBlockedCall("DK5EN-12"));
    TEST_ASSERT_TRUE(isBlockedCall("dk5en-12"));
}

static void test_ssid_entry_blocks_only_that_ssid(void)
{
    blockListSet("DK5EN-1");
    TEST_ASSERT_TRUE(isBlockedCall("DK5EN-1"));
    TEST_ASSERT_TRUE(isBlockedCall("DK5EN-01"));
    TEST_ASSERT_FALSE(isBlockedCall("DK5EN"));
    TEST_ASSERT_FALSE(isBlockedCall("DK5EN-0"));
    TEST_ASSERT_FALSE(isBlockedCall("DK5EN-11"));
    TEST_ASSERT_FALSE(isBlockedCall("DK5EN-2"));
}

static void test_no_prefix_or_substring_match(void)
{
    blockListSet("DK5EN,XX0AAA-1");
    TEST_ASSERT_FALSE(isBlockedCall("DK5ENX"));
    TEST_ASSERT_FALSE(isBlockedCall("DK5E"));
    TEST_ASSERT_FALSE(isBlockedCall("XDK5EN"));
    TEST_ASSERT_FALSE(isBlockedCall("XX0AAA-12"));
}

static void test_ssid_zero_token_is_bare(void)
{
    blockListSet("XX0AAA-1,DL1AB");
    TEST_ASSERT_TRUE(isBlockedCall("XX0AAA-01"));
    TEST_ASSERT_TRUE(isBlockedCall("DL1AB-0"));    // bare call, bare entry
    TEST_ASSERT_FALSE(isBlockedCall("XX0AAA-0"));  // bare call, SSID entry
}

static void test_non_calls_never_match(void)
{
    blockListSet("DK5EN,TEST1");
    TEST_ASSERT_FALSE(isBlockedCall("*"));
    TEST_ASSERT_FALSE(isBlockedCall("H"));
    TEST_ASSERT_FALSE(isBlockedCall("TEST"));
    TEST_ASSERT_FALSE(isBlockedCall("WLNK-1"));
    TEST_ASSERT_FALSE(isBlockedCall(""));
    TEST_ASSERT_FALSE(isBlockedCall(nullptr));
}

static void test_path_relay_blocked(void)
{
    blockListSet("DK5EN");
    TEST_ASSERT_TRUE(isBlockedInPath("DL1AB,DK5EN-12*,OE1X"));
}

static void test_path_spaces_and_star(void)
{
    blockListSet("DK5EN-12");
    TEST_ASSERT_TRUE(isBlockedInPath("DL1AB , DK5EN-12 * , DL1ABC"));
    TEST_ASSERT_TRUE(isBlockedInPath("DL1AB,dk5en-12*"));
}

static void test_path_first_and_last(void)
{
    blockListSet("DK5EN");
    TEST_ASSERT_TRUE(isBlockedInPath("DK5EN,DL1AB*,DL1ABC*"));
    TEST_ASSERT_TRUE(isBlockedInPath("DL1AB,DL1ABC*,DK5EN-7*"));
    TEST_ASSERT_TRUE(isBlockedInPath("DK5EN"));
}

static void test_path_no_false_hit(void)
{
    blockListSet("DK5EN,DL1ABC-1");
    TEST_ASSERT_FALSE(isBlockedInPath("DK5ENX,XDK5EN*,DL1ABC-2*,DL1ABC*"));
    TEST_ASSERT_FALSE(isBlockedInPath("DL1AB,*,H,"));
    TEST_ASSERT_FALSE(isBlockedInPath(""));
    TEST_ASSERT_FALSE(isBlockedInPath(nullptr));
}

static void test_frame_src_or_path(void)
{
    blockListSet("DK5EN");
    TEST_ASSERT_TRUE(isBlockedFrame("DK5EN-1", "DK5EN-1"));
    TEST_ASSERT_TRUE(isBlockedFrame("DK5EN-1", "DL1ABC"));
    TEST_ASSERT_TRUE(isBlockedFrame("DL1AB", "DL1ABC,DK5EN*"));
    TEST_ASSERT_FALSE(isBlockedFrame("DL1AB", "DL1ABC*"));
}

static void test_lenient_load_keeps_good_entries(void)
{
    char out[BLOCKLIST_CSV_MAX];
    uint8_t n = blockListLoadLenient("DK5EN,BAD,dl1abc-01,XX,DK5EN-0,123456", out, sizeof(out), captureDrop);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_STRING("DK5EN,DL1ABC-1", out);
    TEST_ASSERT_EQUAL_INT(3, s_ndropped);
    TEST_ASSERT_EQUAL_STRING("BAD", s_dropped[0]);
    TEST_ASSERT_EQUAL_STRING("XX", s_dropped[1]);
    TEST_ASSERT_EQUAL_STRING("123456", s_dropped[2]);
}

static void test_lenient_load_caps_and_empty(void)
{
    char in[400] = "", out[BLOCKLIST_CSV_MAX];
    for (int i = 0; i < 18; i++)
        snprintf(in + strlen(in), sizeof(in) - strlen(in), "%sABCD%02d", i ? "," : "", i);
    TEST_ASSERT_EQUAL_UINT8(16, blockListLoadLenient(in, out, sizeof(out), captureDrop));
    TEST_ASSERT_EQUAL_INT(2, s_ndropped);
    TEST_ASSERT_EQUAL_UINT8(0, blockListLoadLenient("", out, sizeof(out), captureDrop));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_UINT8(0, blockListLoadLenient(nullptr, out, sizeof(out), nullptr));
}

static void test_drop_counter(void)
{
    uint32_t before = blockListDrops();
    blockListNoteDrop();
    blockListNoteDrop();
    TEST_ASSERT_EQUAL_UINT32(before + 2, blockListDrops());
}

// A save that does not reach flash must be reported (web GUI shows FAIL).
static void test_failed_save_is_reported(void)
{
    blockListTestFailSave(true);
    TEST_ASSERT_FALSE(blockListSave());
    TEST_ASSERT_FALSE(blockListLastSaveOk());
    blockListTestFailSave(false);
    TEST_ASSERT_TRUE(blockListSave());
    TEST_ASSERT_TRUE(blockListLastSaveOk());
}

static void test_host_persistence_stubs(void)
{
    blockListSet("DK5EN");
    TEST_ASSERT_TRUE(blockListSave());
    blockListLoad();   // no-op on host
    TEST_ASSERT_EQUAL_UINT8(1, blockListCount());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_calls);
    RUN_TEST(test_invalid_calls);
    RUN_TEST(test_lower_case);
    RUN_TEST(test_ssid_normalisation);
    RUN_TEST(test_small_out_buffer_rejected);
    RUN_TEST(test_parse_separators_and_spaces);
    RUN_TEST(test_parse_empty_tokens);
    RUN_TEST(test_parse_duplicates);
    RUN_TEST(test_parse_none);
    RUN_TEST(test_parse_limit);
    RUN_TEST(test_parse_duplicate_does_not_count_toward_limit);
    RUN_TEST(test_overlong_token_not_clipped_valid);
    RUN_TEST(test_parse_reports_bad_token);
    RUN_TEST(test_set_failure_keeps_old_list);
    RUN_TEST(test_set_none_clears);
    RUN_TEST(test_empty_list_blocks_nothing);
    RUN_TEST(test_bare_entry_blocks_any_ssid);
    RUN_TEST(test_ssid_entry_blocks_only_that_ssid);
    RUN_TEST(test_no_prefix_or_substring_match);
    RUN_TEST(test_ssid_zero_token_is_bare);
    RUN_TEST(test_non_calls_never_match);
    RUN_TEST(test_path_relay_blocked);
    RUN_TEST(test_path_spaces_and_star);
    RUN_TEST(test_path_first_and_last);
    RUN_TEST(test_path_no_false_hit);
    RUN_TEST(test_frame_src_or_path);
    RUN_TEST(test_lenient_load_keeps_good_entries);
    RUN_TEST(test_lenient_load_caps_and_empty);
    RUN_TEST(test_drop_counter);
    RUN_TEST(test_failed_save_is_reported);
    RUN_TEST(test_host_persistence_stubs);
    return UNITY_END();
}
