// F4-1a: board identity marker formatter (src/mc_board_id.cpp).
#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "mc_board_id.h"

static mc_board_id_t makeId(const char *env, const char *tag, uint16_t hwid)
{
    mc_board_id_t id;
    memset(&id, 0, sizeof(id));
    memcpy(id.magic, "MCBOARD1", 8);
    id.format = 1;
    id.hwid = hwid;
    memcpy(id.env, env, strnlen(env, sizeof(id.env)));
    memcpy(id.tag, tag, strnlen(tag, sizeof(id.tag)));
    return id;
}

void setUp(void) {}
void tearDown(void) {}

static void test_layout(void)
{
    TEST_ASSERT_EQUAL(128, sizeof(mc_board_id_t));
}

static void test_normal(void)
{
    mc_board_id_t id = makeId("heltec_wifi_lora_32_V3", "v4.40a.10.20", 43);
    char out[160];
    int n = mcBoardIdLine(out, sizeof(out), 43, &id);
    TEST_ASSERT_EQUAL_STRING("MCBOARD:v1:43:43:heltec_wifi_lora_32_V3:v4.40a.10.20", out);
    TEST_ASSERT_EQUAL((int)strlen(out), n);
}

static void test_empty_tag(void)
{
    mc_board_id_t id = makeId("ttgo_tbeam", "", 7);
    char out[160];
    mcBoardIdLine(out, sizeof(out), 9, &id);
    TEST_ASSERT_EQUAL_STRING("MCBOARD:v1:9:7:ttgo_tbeam:(local)", out);
}

static void test_env_32_no_nul(void)
{
    mc_board_id_t id;
    memset(&id, 'x', sizeof(id)); // no NUL anywhere: env and tag unterminated
    id.format = 1;
    id.hwid = 1;
    char out[300];
    memset(out, 0x55, sizeof(out));
    int n = mcBoardIdLine(out, sizeof(out), 1, &id);
    char expect[300];
    snprintf(expect, sizeof(expect), "MCBOARD:v1:1:1:%.32s:%.24s",
             "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    TEST_ASSERT_EQUAL_STRING(expect, out);
    TEST_ASSERT_EQUAL((int)strlen(expect), n);
}

static void test_small_buffer(void)
{
    mc_board_id_t id = makeId("heltec_wifi_lora_32_V3", "v4.40a.10.20", 43);
    char out[12 + 4];
    memset(out, 0x55, sizeof(out));
    mcBoardIdLine(out, 12, 43, &id);
    TEST_ASSERT_EQUAL_STRING("MCBOARD:v1:", out);
    TEST_ASSERT_EQUAL_HEX8(0x55, (uint8_t)out[12]); // untouched past n
    char one[1] = {'z'};
    mcBoardIdLine(one, 1, 43, &id);
    TEST_ASSERT_EQUAL('\0', one[0]);
    mcBoardIdLine(NULL, 0, 43, &id); // must not crash
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_layout);
    RUN_TEST(test_normal);
    RUN_TEST(test_empty_tag);
    RUN_TEST(test_env_32_no_nul);
    RUN_TEST(test_small_buffer);
    return UNITY_END();
}
