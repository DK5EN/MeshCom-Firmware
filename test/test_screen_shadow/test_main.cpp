#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../src/t-deck/screen_shadow.h"

static const int W = 8;
static const int H = 4;
static uint8_t fb[W * H * 2];

void setUp(void) { memset(fb, 0, sizeof(fb)); }
void tearDown(void) {}

static uint16_t px(int x, int y) { return (uint16_t)((fb[(y * W + x) * 2] << 8) | fb[(y * W + x) * 2 + 1]); }

static void fill(uint8_t *src, int w, int h)
{
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            src[(y * w + x) * 2] = (uint8_t)(0xA0 + y);
            src[(y * w + x) * 2 + 1] = (uint8_t)x;
        }
}

void test_partial_area_lands_at_its_place(void)
{
    uint8_t src[2 * 2 * 2];
    fill(src, 2, 2);
    screenShadowBlit(fb, W, H, 3, 1, 2, 2, src);
    TEST_ASSERT_EQUAL_HEX16(0xA000, px(3, 1));
    TEST_ASSERT_EQUAL_HEX16(0xA001, px(4, 1));
    TEST_ASSERT_EQUAL_HEX16(0xA100, px(3, 2));
    TEST_ASSERT_EQUAL_HEX16(0xA101, px(4, 2));
    TEST_ASSERT_EQUAL_HEX16(0, px(2, 1));
    TEST_ASSERT_EQUAL_HEX16(0, px(5, 1));
    TEST_ASSERT_EQUAL_HEX16(0, px(3, 0));
    TEST_ASSERT_EQUAL_HEX16(0, px(3, 3));
}

void test_full_frame_is_copied_unchanged(void)
{
    uint8_t src[W * H * 2];
    fill(src, W, H);
    screenShadowBlit(fb, W, H, 0, 0, W, H, src);
    TEST_ASSERT_EQUAL_MEMORY(src, fb, sizeof(fb));
}

void test_right_and_bottom_overhang_is_clipped_and_keeps_the_stride(void)
{
    uint8_t src[3 * 3 * 2];
    fill(src, 3, 3);
    uint8_t guard[sizeof(fb) + 8];
    memset(guard, 0x55, sizeof(guard));
    screenShadowBlit(guard, W, H, 6, 2, 3, 3, src);
    TEST_ASSERT_EQUAL_HEX8(0xA1, guard[(3 * W + 6) * 2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, guard[(3 * W + 6) * 2 + 1]);
    for (size_t i = sizeof(fb); i < sizeof(guard); i++)
        TEST_ASSERT_EQUAL_HEX8(0x55, guard[i]);
}

void test_left_and_top_overhang_skips_the_source_pixels(void)
{
    uint8_t src[3 * 3 * 2];
    fill(src, 3, 3);
    screenShadowBlit(fb, W, H, -1, -1, 3, 3, src);
    TEST_ASSERT_EQUAL_HEX16(0xA101, px(0, 0));
    TEST_ASSERT_EQUAL_HEX16(0xA102, px(1, 0));
    TEST_ASSERT_EQUAL_HEX16(0xA201, px(0, 1));
}

void test_area_outside_or_null_changes_nothing(void)
{
    uint8_t src[2] = {0xFF, 0xFF};
    uint8_t zero[sizeof(fb)];
    memset(zero, 0, sizeof(zero));
    screenShadowBlit(fb, W, H, W, 0, 1, 1, src);
    screenShadowBlit(fb, W, H, 0, H, 1, 1, src);
    screenShadowBlit(fb, W, H, -1, 0, 1, 1, src);
    screenShadowBlit(fb, W, H, 0, 0, 0, 1, src);
    screenShadowBlit(fb, W, H, 0, 0, 1, 1, NULL);
    screenShadowBlit(NULL, W, H, 0, 0, 1, 1, src);
    TEST_ASSERT_EQUAL_MEMORY(zero, fb, sizeof(fb));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_partial_area_lands_at_its_place);
    RUN_TEST(test_full_frame_is_copied_unchanged);
    RUN_TEST(test_right_and_bottom_overhang_is_clipped_and_keeps_the_stride);
    RUN_TEST(test_left_and_top_overhang_skips_the_source_pixels);
    RUN_TEST(test_area_outside_or_null_changes_nothing);
    return UNITY_END();
}
