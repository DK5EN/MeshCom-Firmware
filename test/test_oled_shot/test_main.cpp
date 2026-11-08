// Unity tests of src/oled_shot.h: the u8g2 vertical-byte layout (8 pages x 128 columns, LSB top).
#include <unity.h>
#include <string.h>
#include "../../src/oled_shot.h"
#include "../../src/b64enc.h"   // CLK-01: --oledshot / --tdshot line encoder

void setUp(void) {}
void tearDown(void) {}

static uint8_t g_buf[OLED_SHOT_BYTES];

static void test_size(void)
{
    TEST_ASSERT_EQUAL_INT(1024, OLED_SHOT_BYTES);
}

static void test_corners(void)
{
    memset(g_buf, 0, sizeof(g_buf));
    g_buf[0] = 0x01;                          // page 0, x 0, bit 0 -> (0,0)
    g_buf[127] = 0x80;                        // page 0, x 127, bit 7 -> (127,7)
    g_buf[7 * 128] = 0x01;                    // page 7, x 0, bit 0 -> (0,56)
    g_buf[7 * 128 + 127] = 0x80;              // page 7, x 127, bit 7 -> (127,63)
    TEST_ASSERT_EQUAL_UINT8(1, oledShotPixel(g_buf, 128, 0, 0));
    TEST_ASSERT_EQUAL_UINT8(1, oledShotPixel(g_buf, 128, 127, 7));
    TEST_ASSERT_EQUAL_UINT8(1, oledShotPixel(g_buf, 128, 0, 56));
    TEST_ASSERT_EQUAL_UINT8(1, oledShotPixel(g_buf, 128, 127, 63));
    TEST_ASSERT_EQUAL_UINT8(0, oledShotPixel(g_buf, 128, 1, 0));
    TEST_ASSERT_EQUAL_UINT8(0, oledShotPixel(g_buf, 128, 127, 6));
    TEST_ASSERT_EQUAL_UINT8(0, oledShotPixel(g_buf, 128, 126, 63));
}

static void test_page_boundary(void)
{
    // 0x80 in page 0 lights y=7, not y=8; 0x01 in page 1 lights y=8.
    memset(g_buf, 0, sizeof(g_buf));
    g_buf[5] = 0x80;
    g_buf[128 + 5] = 0x01;
    TEST_ASSERT_EQUAL_UINT8(1, oledShotPixel(g_buf, 128, 5, 7));
    TEST_ASSERT_EQUAL_UINT8(1, oledShotPixel(g_buf, 128, 5, 8));
    TEST_ASSERT_EQUAL_UINT8(0, oledShotPixel(g_buf, 128, 5, 6));
    TEST_ASSERT_EQUAL_UINT8(0, oledShotPixel(g_buf, 128, 5, 9));
    TEST_ASSERT_EQUAL_UINT8(0, oledShotPixel(g_buf, 128, 6, 7));
}

static void test_lsb_is_top(void)
{
    memset(g_buf, 0, sizeof(g_buf));
    g_buf[3 * 128 + 10] = 0x0F;               // y 24..27 lit in column 10
    for (int y = 24; y < 32; y++)
        TEST_ASSERT_EQUAL_UINT8(y < 28 ? 1 : 0, oledShotPixel(g_buf, 128, 10, y));
}

static void test_known_pattern_8bpp(void)
{
    // Diagonal y = x for x < 64, as a font would never draw it: tests the whole mapping.
    memset(g_buf, 0, sizeof(g_buf));
    for (int x = 0; x < 64; x++)
        g_buf[(x >> 3) * 128 + x] |= (uint8_t)(1u << (x & 7));
    static uint8_t img[OLED_SHOT_W * OLED_SHOT_H];
    oledShotTo8bpp(g_buf, OLED_SHOT_W, OLED_SHOT_H, img);
    int lit = 0;
    for (int y = 0; y < OLED_SHOT_H; y++)
        for (int x = 0; x < OLED_SHOT_W; x++)
        {
            uint8_t want = (x < 64 && x == y) ? 255 : 0;
            if (img[y * OLED_SHOT_W + x] != want)
                TEST_FAIL_MESSAGE("8bpp pixel mismatch");
            lit += img[y * OLED_SHOT_W + x] ? 1 : 0;
        }
    TEST_ASSERT_EQUAL_INT(64, lit);
}

// RFC 4648 section 10 test vectors
static void test_b64_rfc4648(void)
{
    static const char *const in[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    static const char *const ex[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    char out[16];
    for (int i = 0; i < 7; i++)
    {
        size_t n = b64Encode((const uint8_t *)in[i], strlen(in[i]), out, sizeof(out));
        TEST_ASSERT_EQUAL_UINT32(strlen(ex[i]), n);
        TEST_ASSERT_EQUAL_STRING(ex[i], out);
    }
}

static void test_b64_capacity(void)
{
    char out[8];
    // "foo" needs 4 chars + NUL = 5; cap 4 is too small -> 0, out terminated
    TEST_ASSERT_EQUAL_UINT32(0, b64Encode((const uint8_t *)"foo", 3, out, 4));
    TEST_ASSERT_EQUAL_UINT32(4, b64Encode((const uint8_t *)"foo", 3, out, 5));
    TEST_ASSERT_EQUAL_UINT32(0, b64Encode((const uint8_t *)"foo", 3, out, 0));
    TEST_ASSERT_EQUAL_UINT32(4, b64EncodedLen(1));
    TEST_ASSERT_EQUAL_UINT32(8, b64EncodedLen(4));
}

static void test_b64_binary(void)
{
    const uint8_t v[] = {0x00, 0xFF, 0xFE, 0x80, 0x01};
    char out[16];
    b64Encode(v, sizeof(v), out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("AP/+gAE=", out);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_size);
    RUN_TEST(test_corners);
    RUN_TEST(test_page_boundary);
    RUN_TEST(test_lsb_is_top);
    RUN_TEST(test_known_pattern_8bpp);
    RUN_TEST(test_b64_rfc4648);
    RUN_TEST(test_b64_capacity);
    RUN_TEST(test_b64_binary);
    return UNITY_END();
}
