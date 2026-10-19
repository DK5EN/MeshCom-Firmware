// F4-2a: Safeboot v3 identity of the installed app image (ota_0 marker, src/mc_board_id.h) and the
// serial line commands --id / --info (src/safeboot/safeboot_id.h). Pure functions, host only.
//
//   pio test -e native_safeboot -f test_safeboot_id

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <safeboot/safeboot_id.h>

void setUp(void) {}
void tearDown(void) {}

static void makeImage(uint8_t *b, size_t n, uint16_t hwid, const char *env, const char *tag) {
    memset(b, 0, n);
    b[0] = 0xE9;
    const uint32_t magic = 0xABCD5432u;
    memcpy(b + 0x20, &magic, 4);
    mc_board_id_t id;
    memset(&id, 0, sizeof(id));
    memcpy(id.magic, "MCBOARD1", 8);
    id.format = 1;
    id.hwid = hwid;
    strncpy(id.env, env, sizeof(id.env));
    strncpy(id.tag, tag, sizeof(id.tag));
    memcpy(b + 0x120, &id, sizeof(id));
}

static uint8_t img[SB_ID_HEAD_LEN];

static void test_valid(void) {
    makeImage(img, sizeof(img), 43, "heltec_wifi_lora_32_V3", "v4.40a.10.19");
    SbIdentity id;
    TEST_ASSERT_EQUAL_INT(SB_ID_OK, sbIdParse(img, sizeof(img), &id));
    TEST_ASSERT_EQUAL_UINT16(43, id.hwid);
    TEST_ASSERT_EQUAL_STRING("heltec_wifi_lora_32_V3", id.env);
    TEST_ASSERT_EQUAL_STRING("v4.40a.10.19", id.tag);
    char line[96];
    sbIdLine(line, sizeof(line), id);
    TEST_ASSERT_EQUAL_STRING("MCBOARD:v1:43:43:heltec_wifi_lora_32_V3:v4.40a.10.19", line);
}

static void test_valid_local_and_full_width_fields(void) {
    makeImage(img, sizeof(img), 7, "e", "");
    SbIdentity id;
    TEST_ASSERT_EQUAL_INT(SB_ID_OK, sbIdParse(img, sizeof(img), &id));
    char line[96];
    sbIdLine(line, sizeof(line), id);
    TEST_ASSERT_EQUAL_STRING("MCBOARD:v1:7:7:e:(local)", line);
    // env 32 bytes and tag 24 bytes with no NUL terminator
    makeImage(img, sizeof(img), 1, "", "");
    memset(img + 0x120 + 12, 'E', 32);
    memset(img + 0x120 + 44, 'T', 24);
    TEST_ASSERT_EQUAL_INT(SB_ID_OK, sbIdParse(img, sizeof(img), &id));
    TEST_ASSERT_EQUAL_size_t(32, strlen(id.env));
    TEST_ASSERT_EQUAL_size_t(24, strlen(id.tag));
}

static void test_erased(void) {
    memset(img, 0xFF, sizeof(img));
    SbIdentity id;
    TEST_ASSERT_EQUAL_INT(SB_ID_ERASED, sbIdParse(img, sizeof(img), &id));
}

static void test_no_image(void) {
    SbIdentity id;
    makeImage(img, sizeof(img), 43, "x", "");
    img[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(SB_ID_NO_IMAGE, sbIdParse(img, sizeof(img), &id));
    makeImage(img, sizeof(img), 43, "x", "");
    img[0x20] ^= 1; // app descriptor magic wrong
    TEST_ASSERT_EQUAL_INT(SB_ID_NO_IMAGE, sbIdParse(img, sizeof(img), &id));
    makeImage(img, sizeof(img), 43, "x", "");
    TEST_ASSERT_EQUAL_INT(SB_ID_NO_IMAGE, sbIdParse(img, SB_ID_HEAD_LEN - 1, &id)); // short read
    TEST_ASSERT_EQUAL_INT(SB_ID_NO_IMAGE, sbIdParse(nullptr, 0, &id));
}

static void test_no_marker(void) {
    SbIdentity id;
    makeImage(img, sizeof(img), 43, "x", "");
    img[0x120] = 'X';
    TEST_ASSERT_EQUAL_INT(SB_ID_NO_MARKER, sbIdParse(img, sizeof(img), &id));
}

static void test_bad_format(void) {
    SbIdentity id;
    makeImage(img, sizeof(img), 43, "x", "");
    img[0x120 + 8] = 2; // format 2
    TEST_ASSERT_EQUAL_INT(SB_ID_BAD_FORMAT, sbIdParse(img, sizeof(img), &id));
}

static void test_reason_names(void) {
    TEST_ASSERT_EQUAL_STRING("erased", sbIdReasonName(SB_ID_ERASED));
    TEST_ASSERT_EQUAL_STRING("no-image", sbIdReasonName(SB_ID_NO_IMAGE));
    TEST_ASSERT_EQUAL_STRING("no-marker", sbIdReasonName(SB_ID_NO_MARKER));
    TEST_ASSERT_EQUAL_STRING("bad-format", sbIdReasonName(SB_ID_BAD_FORMAT));
    TEST_ASSERT_EQUAL_STRING("no-partition", sbIdReasonName(SB_ID_NO_PARTITION));
}

static SbCmd feedStr(SbLineReader &r, const char *s) {
    SbCmd c = SB_CMD_NONE;
    for (; *s; s++) {
        SbCmd x = r.feed(*s);
        if (x != SB_CMD_NONE)
            c = x;
    }
    return c;
}

static void test_reader_commands(void) {
    SbLineReader r;
    TEST_ASSERT_EQUAL_INT(SB_CMD_ID, feedStr(r, "--id\n"));
    TEST_ASSERT_EQUAL_INT(SB_CMD_INFO, feedStr(r, "--info\r"));
    TEST_ASSERT_EQUAL_INT(SB_CMD_ID, feedStr(r, "--id\r\n")); // CRLF: second terminator is an empty line
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, feedStr(r, "--id")); // no terminator yet
    TEST_ASSERT_EQUAL_INT(SB_CMD_ID, r.feed('\n'));
}

static void test_reader_junk(void) {
    SbLineReader r;
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, feedStr(r, "--idx\n"));
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, feedStr(r, "hello\n"));
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, feedStr(r, "\n\n"));
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, feedStr(r, "--info --id\n"));
    TEST_ASSERT_EQUAL_INT(SB_CMD_ID, feedStr(r, "--id\n")); // still works after junk
}

static void test_reader_overlong(void) {
    SbLineReader r;
    // a long line ending in a valid command text must NOT trigger; the reader recovers afterwards
    char longline[128];
    memset(longline, 'a', sizeof(longline));
    strcpy(longline + 100, "--id");
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, feedStr(r, longline));
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, r.feed('\n'));
    TEST_ASSERT_EQUAL_INT(SB_CMD_ID, feedStr(r, "--id\n"));
    // exactly-too-long prefix then the command text
    char l2[64];
    memset(l2, 'b', sizeof(l2));
    l2[sizeof(l2) - 1] = 0;
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, feedStr(r, l2));
    TEST_ASSERT_EQUAL_INT(SB_CMD_NONE, r.feed('\n'));
    TEST_ASSERT_EQUAL_INT(SB_CMD_INFO, feedStr(r, "--info\n"));
}

static void test_respond_id(void) {
    SbIdentity id;
    makeImage(img, sizeof(img), 43, "heltec_wifi_lora_32_V3", "");
    sbIdParse(img, sizeof(img), &id);
    char out[400];
    sbRespond(SB_CMD_ID, id, "DK5EN-90", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("MCBOARD:v1:43:43:heltec_wifi_lora_32_V3:(local)\n", out);
    SbIdentity none;
    sbIdFail(&none, SB_ID_ERASED);
    sbRespond(SB_CMD_ID, none, "DK5EN-90", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("MCBOARD:none:erased\n", out);
}

static void test_respond_info(void) {
    SbIdentity id;
    makeImage(img, sizeof(img), 43, "heltec_wifi_lora_32_V3", "v4.40a.10.19");
    sbIdParse(img, sizeof(img), &id);
    char out[400];
    sbRespond(SB_CMD_INFO, id, "DK5EN-90", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("--MeshCom Safeboot 3\n"
                             "...Call: <DK5EN-90> ...NODE 43 <SAFEBOOT>\n"
                             "...AU env=heltec_wifi_lora_32_V3 tag=v4.40a.10.19\n"
                             "[SAFEBOOT];ver;3\n",
                             out);
    // local build
    makeImage(img, sizeof(img), 43, "e", "");
    sbIdParse(img, sizeof(img), &id);
    sbRespond(SB_CMD_INFO, id, "DK5EN-90", out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strstr(out, "...AU env=e tag=(local)\n"));
    // no valid marker: no NODE and no AU line, version line stays
    SbIdentity none;
    sbIdFail(&none, SB_ID_NO_MARKER);
    sbRespond(SB_CMD_INFO, none, "DK5EN-90", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("--MeshCom Safeboot 3\n...Call: <DK5EN-90>\n[SAFEBOOT];ver;3\n", out);
    // NONE and tiny buffers: empty / truncated, always NUL-terminated
    TEST_ASSERT_EQUAL_size_t(0, sbRespond(SB_CMD_NONE, id, "X", out, sizeof(out)));
    char tiny[8];
    sbRespond(SB_CMD_INFO, id, "X", tiny, sizeof(tiny));
    TEST_ASSERT_EQUAL_size_t(7, strlen(tiny));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_valid);
    RUN_TEST(test_valid_local_and_full_width_fields);
    RUN_TEST(test_erased);
    RUN_TEST(test_no_image);
    RUN_TEST(test_no_marker);
    RUN_TEST(test_bad_format);
    RUN_TEST(test_reason_names);
    RUN_TEST(test_reader_commands);
    RUN_TEST(test_reader_junk);
    RUN_TEST(test_reader_overlong);
    RUN_TEST(test_respond_id);
    RUN_TEST(test_respond_info);
    return UNITY_END();
}
