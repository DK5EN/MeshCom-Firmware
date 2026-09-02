// Native tests for the T-Deck key auto-repeat state machine (TD-10).
// Pure logic, no I2C/LVGL: exercises kbd_repeat.h exactly as documented in
// docs/tdeck-keyrepeat-impl-plan-20260902.md Sec.5.
//
//   pio test -e native -f test_kbd_repeat

#include <unity.h>

#include <stdint.h>

#include "t-deck/kbd_repeat.h"

void setUp(void) {}
void tearDown(void) {}

// -- 1. kbdRawFrameValid -----------------------------------------------

static void test_frame_valid_single_bit_backspace(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08}; // Backspace
    TEST_ASSERT_TRUE(kbdRawFrameValid(f));
}

static void test_frame_valid_single_bit_space(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x20, 0x00, 0x00, 0x00, 0x00}; // Space
    TEST_ASSERT_TRUE(kbdRawFrameValid(f));
}

static void test_frame_valid_two_bits_sym_backspace(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x04, 0x00, 0x00, 0x00, 0x08}; // SYM + Backspace
    TEST_ASSERT_TRUE(kbdRawFrameValid(f));
}

static void test_frame_invalid_old_firmware_idle_high(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(kbdRawFrameValid(f));
}

static void test_frame_invalid_all_zero(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(kbdRawFrameValid(f));
}

static void test_frame_invalid_all_0xff(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(kbdRawFrameValid(f));
}

static void test_frame_invalid_four_bits_set(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x0F, 0x00, 0x00, 0x00, 0x00}; // 4 bits > KBD_RAW_MAX_BITS
    TEST_ASSERT_FALSE(kbdRawFrameValid(f));
}

// -- 2. Arm + hold -------------------------------------------------------

static void test_arm_then_hold_same_frame(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, 1000));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);
    TEST_ASSERT_TRUE(kbdRepeatHold(&s, backspace, 1100));
}

static void test_hold_true_when_extra_modifier_bit_appears(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t backspace[KBD_RAW_FRAME_LEN]     = {0x00, 0x00, 0x00, 0x00, 0x08};
    const uint8_t backspace_sym[KBD_RAW_FRAME_LEN] = {0x04, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, 1000));
    TEST_ASSERT_TRUE(kbdRepeatHold(&s, backspace_sym, 1100));
}

static void test_hold_false_when_key_bit_drops(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};
    const uint8_t released[KBD_RAW_FRAME_LEN]  = {0x00, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, 1000));
    TEST_ASSERT_FALSE(kbdRepeatHold(&s, released, 1100));
}

// -- 3. Timeout ------------------------------------------------------------

static void test_hold_false_at_timeout(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, 1000));
    TEST_ASSERT_FALSE(kbdRepeatHold(&s, backspace, 1000 + KBD_RAW_TIMEOUT_MS));
}

// -- 4. Degradation ----------------------------------------------------

static void test_three_invalid_arms_mark_unsupported(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t old_fw[KBD_RAW_FRAME_LEN] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, old_fw, 0x08, 1000));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, old_fw, 0x08, 1100));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, old_fw, 0x08, 1200));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_NO, s.support);
}

static void test_one_valid_arm_marks_supported_immediately(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, 1000));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);
}

// Fast-typist correction: a key already released by the time the first raw
// frame is read comes back all-zero -- must NOT count as a probe failure
// (an old keyboard's non-response is a 0xFF-padded frame, not all-zero).
static void test_all_zero_frame_does_not_count_as_probe(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t released[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, released, 0x08, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, s.probes);
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
}

// The tdeck_main.cpp arm path fills a short/failed I2C read with 0xFF
// (same shape as the old-firmware idle-high response) so it always counts.
static void test_0xff_padded_frame_three_times_marks_unsupported(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t no_response[KBD_RAW_FRAME_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, 1000));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, 1100));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, 1200));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_NO, s.support);
}

static void test_valid_frame_after_two_failures_marks_supported(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t no_response[KBD_RAW_FRAME_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t backspace[KBD_RAW_FRAME_LEN]   = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, 1000));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, 1100));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);   // 2 misses, not yet PROBE_MAX

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, 1200));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);
}

// -- 5. kbdRepeatClear leaves support/probes untouched ----------------------

static void test_clear_leaves_support_untouched(void)
{
    struct KbdRepeat s;
    memset(&s, 0, sizeof(s));
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, 1000));
    TEST_ASSERT_TRUE(s.active);

    kbdRepeatClear(&s);

    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_EQUAL_UINT32(0, s.key);
    for (int i = 0; i < KBD_RAW_FRAME_LEN; i++)
        TEST_ASSERT_EQUAL_UINT8(0, s.mask[i]);
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);  // per-boot verdict, not per-hold
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_frame_valid_single_bit_backspace);
    RUN_TEST(test_frame_valid_single_bit_space);
    RUN_TEST(test_frame_valid_two_bits_sym_backspace);
    RUN_TEST(test_frame_invalid_old_firmware_idle_high);
    RUN_TEST(test_frame_invalid_all_zero);
    RUN_TEST(test_frame_invalid_all_0xff);
    RUN_TEST(test_frame_invalid_four_bits_set);

    RUN_TEST(test_arm_then_hold_same_frame);
    RUN_TEST(test_hold_true_when_extra_modifier_bit_appears);
    RUN_TEST(test_hold_false_when_key_bit_drops);

    RUN_TEST(test_hold_false_at_timeout);

    RUN_TEST(test_three_invalid_arms_mark_unsupported);
    RUN_TEST(test_one_valid_arm_marks_supported_immediately);
    RUN_TEST(test_all_zero_frame_does_not_count_as_probe);
    RUN_TEST(test_0xff_padded_frame_three_times_marks_unsupported);
    RUN_TEST(test_valid_frame_after_two_failures_marks_supported);

    RUN_TEST(test_clear_leaves_support_untouched);

    return UNITY_END();
}
