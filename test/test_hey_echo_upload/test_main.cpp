// Native suite for hey_echo_upload.h (docs/hey-echo-upload-concept.md): a
// gateway uploads the relayed echo of its own HEY (own-origin frames only);
// nothing else.
//
//   pio test -e native -f test_hey_echo_upload

#include <unity.h>

#include <stdint.h>

#include <hey_echo_upload.h>

#define T_HEY  0x40
#define T_TEXT 0x3A
#define T_POS  0x21

void setUp(void) {}
void tearDown(void) {}

static void test_gateway_hey_foreign_last_hop_uploads(void)
{
    TEST_ASSERT_TRUE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "DK5EN-98", "DK5EN-98"));
}

static void test_non_gateway_never_uploads(void)
{
    TEST_ASSERT_FALSE(heyEchoShouldUpload(false, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "DK5EN-98", "DK5EN-98"));
}

static void test_text_and_position_do_not_upload(void)
{
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_TEXT, T_HEY, '@', false, "DB0XYZ-1", "DK5EN-98", "DK5EN-98"));
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_POS, T_HEY, '@', false, "DB0XYZ-1", "DK5EN-98", "DK5EN-98"));
}

static void test_pn_repeat_does_not_upload(void)
{
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', true, "DB0XYZ-1", "DK5EN-98", "DK5EN-98"));
}

static void test_own_last_hop_does_not_upload(void)
{
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DK5EN-98", "DK5EN-98", "DK5EN-98"));
}

static void test_null_or_empty_last_hop_does_not_upload(void)
{
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, nullptr, "DK5EN-98", "DK5EN-98"));
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "", "DK5EN-98", "DK5EN-98"));
}

static void test_null_or_empty_own_call_does_not_upload(void)
{
    // an own-origin check against no call cannot pass
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "DK5EN-98", nullptr));
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "", ""));
}

static void test_foreign_source_call_does_not_upload(void)
{
    // server-forwarded foreign HEY (insertOwnTx) relayed back by a neighbour
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "DL1ABC-11", "DK5EN-98"));
    // exact compare: SSID and prefix matches are not own
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "DK5EN-9", "DK5EN-98"));
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "DK5EN-98x", "DK5EN-98"));
}

static void test_non_hey_payload_type_does_not_upload(void)
{
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, ':', false, "DB0XYZ-1", "DK5EN-98", "DK5EN-98"));
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '!', false, "DB0XYZ-1", "DK5EN-98", "DK5EN-98"));
}

static void test_null_or_empty_source_call_does_not_upload(void)
{
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", nullptr, "DK5EN-98"));
    TEST_ASSERT_FALSE(heyEchoShouldUpload(true, T_HEY, T_HEY, '@', false, "DB0XYZ-1", "", "DK5EN-98"));
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_gateway_hey_foreign_last_hop_uploads);
    RUN_TEST(test_non_gateway_never_uploads);
    RUN_TEST(test_text_and_position_do_not_upload);
    RUN_TEST(test_pn_repeat_does_not_upload);
    RUN_TEST(test_own_last_hop_does_not_upload);
    RUN_TEST(test_null_or_empty_last_hop_does_not_upload);
    RUN_TEST(test_null_or_empty_own_call_does_not_upload);
    RUN_TEST(test_foreign_source_call_does_not_upload);
    RUN_TEST(test_non_hey_payload_type_does_not_upload);
    RUN_TEST(test_null_or_empty_source_call_does_not_upload);
    return UNITY_END();
}
