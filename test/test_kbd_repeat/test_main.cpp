// Native tests for the T-Deck key auto-repeat state machine (TD-10).
// Pure logic, no I2C/LVGL: exercises kbd_repeat.h exactly as documented in
// docs/tdeck-keyrepeat-impl-plan-20260902.md Sec.5 plus the K1/K3/K7 cases
// from docs/review-verdict-tdeck-keyrepeat-20260902.md.
//
//   pio test -e native -f test_kbd_repeat

#include <unity.h>

#include <stdint.h>

#include "t-deck/kbd_repeat.h"

void setUp(void) {}
void tearDown(void) {}

// Backspace = col 4 / row 3, Space = col 0 / row 5, 'a' = col 0 / row 3.
#define BS_COL 4
#define BS_ROW 3
#define SP_COL 0
#define SP_ROW 5

// -- 0. struct layout ---------------------------------------------------

static void test_struct_is_compact(void)
{
    // uint32_t members first, pointer next, bytes last: 20 B on the 32-bit
    // target, 24 B on a 64-bit host. A regression here means somebody
    // reordered the fields and re-introduced padding.
    TEST_ASSERT_TRUE(sizeof(struct KbdRepeat) <= 24);
}

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

static void test_frame_valid_exactly_three_bits(void)
{
    // key + both shifts: KBD_RAW_MAX_BITS, still valid.
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x00, 0x40, 0x08, 0x00, 0x08};
    TEST_ASSERT_TRUE(kbdRawFrameValid(f));
}

static void test_frame_invalid_four_bits_across_bytes(void)
{
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x04, 0x40, 0x08, 0x00, 0x08};
    TEST_ASSERT_FALSE(kbdRawFrameValid(f));
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

static void test_frame_invalid_bit7_set(void)
{
    // Only rows 0..6 exist, so bit 7 is never set by a raw-mode keyboard.
    const uint8_t f[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x80};
    TEST_ASSERT_FALSE(kbdRawFrameValid(f));
}

// -- 1b. kbdExpectedCell ------------------------------------------------

static void test_cell_backspace(void)
{
    uint8_t c = 9, r = 9;
    TEST_ASSERT_TRUE(kbdExpectedCell(0x08, &c, &r));
    TEST_ASSERT_EQUAL_UINT8(4, c);
    TEST_ASSERT_EQUAL_UINT8(3, r);
}

static void test_cell_space(void)
{
    uint8_t c = 9, r = 9;
    TEST_ASSERT_TRUE(kbdExpectedCell(' ', &c, &r));
    TEST_ASSERT_EQUAL_UINT8(0, c);
    TEST_ASSERT_EQUAL_UINT8(5, r);
}

static void test_cell_lowercase_a(void)
{
    uint8_t c = 9, r = 9;
    TEST_ASSERT_TRUE(kbdExpectedCell('a', &c, &r));
    TEST_ASSERT_EQUAL_UINT8(0, c);
    TEST_ASSERT_EQUAL_UINT8(3, r);
}

static void test_cell_uppercase_A_maps_to_base_cell(void)
{
    uint8_t c = 9, r = 9;
    TEST_ASSERT_TRUE(kbdExpectedCell('A', &c, &r));
    TEST_ASSERT_EQUAL_UINT8(0, c);
    TEST_ASSERT_EQUAL_UINT8(3, r);
}

static void test_cell_hash_from_symbol_table(void)
{
    // '#' has no base cell; it lives on keyboard_symbol[0][0] (the 'q' key).
    uint8_t c = 9, r = 9;
    TEST_ASSERT_TRUE(kbdExpectedCell('#', &c, &r));
    TEST_ASSERT_EQUAL_UINT8(0, c);
    TEST_ASSERT_EQUAL_UINT8(0, r);
}

static void test_cell_dollar_from_base_table(void)
{
    uint8_t c = 9, r = 9;
    TEST_ASSERT_TRUE(kbdExpectedCell('$', &c, &r));
    TEST_ASSERT_EQUAL_UINT8(4, c);
    TEST_ASSERT_EQUAL_UINT8(4, r);
}

static void test_cell_unknown_bytes(void)
{
    uint8_t c = 9, r = 9;
    TEST_ASSERT_FALSE(kbdExpectedCell(0x00, &c, &r));   // no key
    TEST_ASSERT_FALSE(kbdExpectedCell(0x0D, &c, &r));   // Enter: no character cell
    TEST_ASSERT_FALSE(kbdExpectedCell(0x0C, &c, &r));   // Alt+C
    TEST_ASSERT_FALSE(kbdExpectedCell(0xE4, &c, &r));   // non-ASCII
}

// -- 2. Arm + hold -------------------------------------------------------

static void test_arm_then_hold_same_frame(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_ACTIVE, kbdRepeatHold(&s, backspace, 1100));
}

static void test_hold_true_when_extra_modifier_bit_appears(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN]     = {0x00, 0x00, 0x00, 0x00, 0x08};
    const uint8_t backspace_sym[KBD_RAW_FRAME_LEN] = {0x04, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_ACTIVE, kbdRepeatHold(&s, backspace_sym, 1100));
}

static void test_hold_released_when_key_bit_drops(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};
    const uint8_t released[KBD_RAW_FRAME_LEN]  = {0x00, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_RELEASED, kbdRepeatHold(&s, released, 1100));
}

static void test_hold_released_when_bit_moves_to_another_column(void)
{
    // Finger rolled from Backspace (4/3) onto Enter (3/3): the armed bit is
    // gone even though the frame still has exactly one bit set.
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};
    const uint8_t enter[KBD_RAW_FRAME_LEN]     = {0x00, 0x00, 0x00, 0x08, 0x00};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_RELEASED, kbdRepeatHold(&s, enter, 1100));
}

static void test_hold_released_when_not_active(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_EQUAL_INT(KBD_HOLD_RELEASED, kbdRepeatHold(&s, backspace, 1000));
}

// -- 3. Timeout ------------------------------------------------------------

static void test_hold_active_one_ms_before_timeout(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_ACTIVE, kbdRepeatHold(&s, backspace, 1000 + KBD_RAW_TIMEOUT_MS - 1));
}

static void test_hold_timeout_at_boundary(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_TIMEOUT, kbdRepeatHold(&s, backspace, 1000 + KBD_RAW_TIMEOUT_MS));
}

static void test_hold_timeout_wins_over_released_frame(void)
{
    // The reason ladder must report `timeout`, not `release`, when both apply.
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};
    const uint8_t released[KBD_RAW_FRAME_LEN]  = {0x00, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_TIMEOUT, kbdRepeatHold(&s, released, 1000 + KBD_RAW_TIMEOUT_MS));
}

static void test_hold_survives_millis_rollover(void)
{
    struct KbdRepeat s = {};
    const uint8_t space[KBD_RAW_FRAME_LEN] = {0x20, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, space, ' ', SP_COL, SP_ROW, 0xFFFFFF00u));
    // 0xFFFFFF00 -> 0x100 is 512 ms of unsigned wrap-around, well inside the window.
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_ACTIVE, kbdRepeatHold(&s, space, 0x100u));
    // ... and the timeout still fires on the far side of the wrap.
    TEST_ASSERT_EQUAL_INT(KBD_HOLD_TIMEOUT, kbdRepeatHold(&s, space, 0xFFFFFF00u + KBD_RAW_TIMEOUT_MS));
}

// -- 4. Arm rejects a frame that does not show the key itself (K1) --------

static void test_arm_rejects_frame_without_expected_bit(void)
{
    // Key 'a' delivered, but the frame shows Backspace: a fast a->Backspace
    // transition must not bind 'a' to the Backspace bit.
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};
    uint8_t c = 0, r = 0;

    TEST_ASSERT_TRUE(kbdExpectedCell('a', &c, &r));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, backspace, 'a', c, r, 1000));
    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_EQUAL_UINT8(1, s.probes);          // a wrong-bit frame is a probe failure
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
}

static void test_arm_accepts_own_bit_with_modifier(void)
{
    struct KbdRepeat s = {};
    const uint8_t shift_a[KBD_RAW_FRAME_LEN] = {0x08, 0x40, 0x00, 0x00, 0x00}; // 'a' + LSHIFT
    uint8_t c = 0, r = 0;

    TEST_ASSERT_TRUE(kbdExpectedCell('A', &c, &r));
    TEST_ASSERT_TRUE(kbdRepeatArm(&s, shift_a, 'A', c, r, 1000));
    TEST_ASSERT_TRUE(s.active);
    TEST_ASSERT_EQUAL_UINT32('A', s.key);
}

static void test_arm_rejects_bit7_frame_and_counts_probe(void)
{
    struct KbdRepeat s = {};
    const uint8_t bogus[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x88}; // >0x7F, bit 3 set

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, bogus, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_UINT8(1, s.probes);
}

// -- 5. Degradation and K3 ------------------------------------------------

static void test_three_invalid_arms_mark_unsupported(void)
{
    struct KbdRepeat s = {};
    const uint8_t old_fw[KBD_RAW_FRAME_LEN] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, old_fw, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, old_fw, 0x08, BS_COL, BS_ROW, 1100));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, old_fw, 0x08, BS_COL, BS_ROW, 1200));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_NO, s.support);
}

static void test_one_valid_arm_marks_supported_immediately(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);
}

// Fast-typist correction: a key already released by the time the first raw
// frame is read comes back all-zero -- must NOT count as a probe failure
// (an old keyboard's non-response is a 0xFF-padded frame, not all-zero).
static void test_all_zero_frame_does_not_count_as_probe(void)
{
    struct KbdRepeat s = {};
    const uint8_t released[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, released, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_UINT8(0, s.probes);
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
}

// A key-mode slave answers a 5-byte request with five zeros for the whole
// boot: that must never exhaust the probe budget on its own.
static void test_many_all_zero_frames_never_mark_unsupported(void)
{
    struct KbdRepeat s = {};
    const uint8_t released[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x00};

    for (int i = 0; i < 20; i++)
        TEST_ASSERT_FALSE(kbdRepeatArm(&s, released, 0x08, BS_COL, BS_ROW, (uint32_t)(1000 + i)));
    TEST_ASSERT_EQUAL_UINT8(0, s.probes);
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);
}

// The tdeck_main.cpp arm path fills a failed I2C read with 0xFF (same shape
// as the old-firmware idle-high response) so it always counts.
static void test_0xff_padded_frame_three_times_marks_unsupported(void)
{
    struct KbdRepeat s = {};
    const uint8_t no_response[KBD_RAW_FRAME_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, BS_COL, BS_ROW, 1100));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, BS_COL, BS_ROW, 1200));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_NO, s.support);
}

static void test_valid_frame_after_two_failures_marks_supported(void)
{
    struct KbdRepeat s = {};
    const uint8_t no_response[KBD_RAW_FRAME_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t backspace[KBD_RAW_FRAME_LEN]   = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, BS_COL, BS_ROW, 1100));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_UNKNOWN, s.support);   // 2 misses, not yet PROBE_MAX

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1200));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);
}

// K3a: a successful arm resets the probe budget, so scattered bad frames
// across a boot can never add up to a NO verdict.
static void test_successful_arm_resets_probe_counter(void)
{
    struct KbdRepeat s = {};
    const uint8_t no_response[KBD_RAW_FRAME_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t backspace[KBD_RAW_FRAME_LEN]   = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_FALSE(kbdRepeatArm(&s, no_response, 0x08, BS_COL, BS_ROW, 1100));
    TEST_ASSERT_EQUAL_UINT8(2, s.probes);

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1200));
    TEST_ASSERT_EQUAL_UINT8(0, s.probes);
}

// K3b: once the keyboard has proven itself, a ghosted or wrong-bit frame
// only declines that one arm -- it must not disable repeat for the boot.
static void test_failures_after_yes_never_regress_the_verdict(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};
    const uint8_t ghosted[KBD_RAW_FRAME_LEN]   = {0x0F, 0x00, 0x00, 0x00, 0x08}; // 5 bits

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);

    for (int i = 0; i < 10; i++)
        TEST_ASSERT_FALSE(kbdRepeatArm(&s, ghosted, 0x08, BS_COL, BS_ROW, (uint32_t)(2000 + i)));

    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);
    TEST_ASSERT_EQUAL_UINT8(0, s.probes);
}

// -- 6. kbdRepeatClear leaves support/probes untouched ----------------------

static void test_clear_leaves_support_untouched(void)
{
    struct KbdRepeat s = {};
    const uint8_t backspace[KBD_RAW_FRAME_LEN] = {0x00, 0x00, 0x00, 0x00, 0x08};

    TEST_ASSERT_TRUE(kbdRepeatArm(&s, backspace, 0x08, BS_COL, BS_ROW, 1000));
    TEST_ASSERT_TRUE(s.active);
    s.focus = (void *)&s;                             // stand-in for the LVGL object

    kbdRepeatClear(&s);

    TEST_ASSERT_FALSE(s.active);
    TEST_ASSERT_EQUAL_UINT32(0, s.key);
    TEST_ASSERT_NULL(s.focus);
    for (int i = 0; i < KBD_RAW_FRAME_LEN; i++)
        TEST_ASSERT_EQUAL_UINT8(0, s.mask[i]);
    TEST_ASSERT_EQUAL_UINT8(KBD_RAW_YES, s.support);  // per-boot verdict, not per-hold
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_struct_is_compact);

    RUN_TEST(test_frame_valid_single_bit_backspace);
    RUN_TEST(test_frame_valid_single_bit_space);
    RUN_TEST(test_frame_valid_two_bits_sym_backspace);
    RUN_TEST(test_frame_valid_exactly_three_bits);
    RUN_TEST(test_frame_invalid_four_bits_across_bytes);
    RUN_TEST(test_frame_invalid_old_firmware_idle_high);
    RUN_TEST(test_frame_invalid_all_zero);
    RUN_TEST(test_frame_invalid_all_0xff);
    RUN_TEST(test_frame_invalid_four_bits_set);
    RUN_TEST(test_frame_invalid_bit7_set);

    RUN_TEST(test_cell_backspace);
    RUN_TEST(test_cell_space);
    RUN_TEST(test_cell_lowercase_a);
    RUN_TEST(test_cell_uppercase_A_maps_to_base_cell);
    RUN_TEST(test_cell_hash_from_symbol_table);
    RUN_TEST(test_cell_dollar_from_base_table);
    RUN_TEST(test_cell_unknown_bytes);

    RUN_TEST(test_arm_then_hold_same_frame);
    RUN_TEST(test_hold_true_when_extra_modifier_bit_appears);
    RUN_TEST(test_hold_released_when_key_bit_drops);
    RUN_TEST(test_hold_released_when_bit_moves_to_another_column);
    RUN_TEST(test_hold_released_when_not_active);

    RUN_TEST(test_hold_active_one_ms_before_timeout);
    RUN_TEST(test_hold_timeout_at_boundary);
    RUN_TEST(test_hold_timeout_wins_over_released_frame);
    RUN_TEST(test_hold_survives_millis_rollover);

    RUN_TEST(test_arm_rejects_frame_without_expected_bit);
    RUN_TEST(test_arm_accepts_own_bit_with_modifier);
    RUN_TEST(test_arm_rejects_bit7_frame_and_counts_probe);

    RUN_TEST(test_three_invalid_arms_mark_unsupported);
    RUN_TEST(test_one_valid_arm_marks_supported_immediately);
    RUN_TEST(test_all_zero_frame_does_not_count_as_probe);
    RUN_TEST(test_many_all_zero_frames_never_mark_unsupported);
    RUN_TEST(test_0xff_padded_frame_three_times_marks_unsupported);
    RUN_TEST(test_valid_frame_after_two_failures_marks_supported);
    RUN_TEST(test_successful_arm_resets_probe_counter);
    RUN_TEST(test_failures_after_yes_never_regress_the_verdict);

    RUN_TEST(test_clear_leaves_support_untouched);

    return UNITY_END();
}
