// Native tests for the GPS-11 on-air grid hysteresis (src/pos_grid_hold.*).
//
//   pio test -e native -f test_pos_grid_hold

#include <unity.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <pos_grid_hold.h>

void setUp(void)    {}
void tearDown(void) {}

#define CELL_DEG (1.0 / 6000.0)

static long cell(double v)
{
    long c = -1;
    TEST_ASSERT_TRUE(posGridCell(v, &c));
    return c;
}

// DDMM.MM -> index: 4800.50 = 48 deg 0.50' = 48*6000 + 50
static long idx(int deg, int hundredths) { return deg * 6000L + hundredths; }

void test_cell_hand_values(void)
{
    TEST_ASSERT_EQUAL_INT32(idx(48, 50), cell(48.0083));   // "4800.50"
    TEST_ASSERT_EQUAL_INT32(idx(48, 49), cell(48.0082));   // "4800.49"
    TEST_ASSERT_EQUAL_INT32(idx(48, 600), cell(48.1));     // "4806.00"
    TEST_ASSERT_EQUAL_INT32(idx(49, 0), cell(48.99999));   // "4900.00"
    // negative: cell of the absolute value
    TEST_ASSERT_EQUAL_INT32(idx(48, 50), cell(-48.0083));
    TEST_ASSERT_EQUAL_INT32(idx(16, 600), cell(-16.1));    // "1606.00"
    TEST_ASSERT_FALSE(posGridCell(NAN, NULL));
}

void test_first_call_emits_input(void)
{
    struct PosGridHold h; double a, b;
    posGridHoldReset(&h);
    posGridHold(&h, 48.0083, 16.1234, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - 48.0083) < 1e-12);
    TEST_ASSERT_TRUE(fabs(b - 16.1234) < 1e-12);
}

void test_hold_within_cell(void)
{
    struct PosGridHold h; double a, b;
    posGridHoldReset(&h);
    posGridHold(&h, 48.00830, 16.1, &a, &b);
    posGridHold(&h, 48.00834, 16.1, &a, &b);   // same cell
    TEST_ASSERT_TRUE(fabs(a - 48.0083) < 1e-12);
}

// actual border (in degrees) between cell c and c+1 near v0 (v0 in cell c, positive)
static double border_above(double v0)
{
    long c0 = cell(v0);
    double lo = v0, hi = v0 + 2 * CELL_DEG;
    for (int i = 0; i < 50; i++)
    {
        double m = 0.5 * (lo + hi);
        if (cell(m) == c0) lo = m; else hi = m;
    }
    return 0.5 * (lo + hi);
}

void test_hold_across_border_in_band(void)
{
    struct PosGridHold h; double a, b;
    double bd = border_above(48.0083);
    posGridHoldReset(&h);
    posGridHold(&h, bd - 0.2 * CELL_DEG, 16.1, &a, &b);
    double first = a;
    posGridHold(&h, bd + 0.2 * CELL_DEG, 16.1, &a, &b);   // next cell, 0.2 beyond
    TEST_ASSERT_TRUE(cell(bd + 0.2 * CELL_DEG) != cell(first));
    TEST_ASSERT_TRUE(fabs(a - first) < 1e-12);
}

void test_release_beyond_band(void)
{
    struct PosGridHold h; double a, b;
    double bd = border_above(48.0083);
    posGridHoldReset(&h);
    posGridHold(&h, bd - 0.2 * CELL_DEG, 16.1, &a, &b);
    double nv = bd + 0.4 * CELL_DEG;
    posGridHold(&h, nv, 16.1, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - nv) < 1e-12);
    // and falling back by 0.2 cells stays in the new cell (hold now on the new one)
    posGridHold(&h, bd - 0.2 * CELL_DEG, 16.1, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - nv) < 1e-12);
    // jump of two cells releases
    double far = nv + 3 * CELL_DEG;
    posGridHold(&h, far, 16.1, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - far) < 1e-12);
}

void test_axes_independent(void)
{
    struct PosGridHold h; double a, b;
    double bd = border_above(48.0083);
    posGridHoldReset(&h);
    posGridHold(&h, bd - 0.2 * CELL_DEG, 16.1000, &a, &b);
    double la0 = a;
    // lat inside band (held), lon moves 5 cells (released)
    posGridHold(&h, bd + 0.2 * CELL_DEG, 16.1000 + 5 * CELL_DEG, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - la0) < 1e-12);
    TEST_ASSERT_TRUE(fabs(b - (16.1000 + 5 * CELL_DEG)) < 1e-12);
}

void test_south_west(void)
{
    struct PosGridHold h; double a, b;
    double bd = border_above(48.0083);   // positive border; mirror it
    posGridHoldReset(&h);
    posGridHold(&h, -(bd - 0.2 * CELL_DEG), -(16.1), &a, &b);
    double first = a;
    posGridHold(&h, -(bd + 0.2 * CELL_DEG), -(16.1), &a, &b);   // held
    TEST_ASSERT_TRUE(fabs(a - first) < 1e-12);
    double nv = -(bd + 0.4 * CELL_DEG);
    posGridHold(&h, nv, -(16.1), &a, &b);                       // released
    TEST_ASSERT_TRUE(fabs(a - nv) < 1e-12);
    // hemisphere change releases
    posGridHold(&h, 48.0, 16.1, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - 48.0) < 1e-12);
}

void test_degree_rollover(void)
{
    struct PosGridHold h; double a, b;
    // border between 4859.99 and 4900.00 (= 48.99991.. deg)
    double bd = border_above(48.9998);
    TEST_ASSERT_EQUAL_INT32(idx(49, 0), cell(bd + 0.5 * CELL_DEG));
    posGridHoldReset(&h);
    posGridHold(&h, bd - 0.2 * CELL_DEG, 16.1, &a, &b);
    double first = a;
    posGridHold(&h, bd + 0.2 * CELL_DEG, 16.1, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - first) < 1e-12);
    posGridHold(&h, bd + 0.5 * CELL_DEG, 16.1, &a, &b);
    TEST_ASSERT_TRUE(fabs(a - (bd + 0.5 * CELL_DEG)) < 1e-12);
}

void test_jitter_no_cell_change(void)
{
    struct PosGridHold h; double a, b;
    double bd = border_above(48.0083);
    posGridHoldReset(&h);
    posGridHold(&h, bd - 0.1 * CELL_DEG, 16.1, &a, &b);
    long last = cell(a), changes = 0, ref_changes = 0, ref_last = last;
    srand(1);
    for (int i = 0; i < 1000; i++)
    {
        double v = bd + ((i & 1) ? 0.1 : -0.1) * CELL_DEG
                      + ((rand() % 1000) / 1000.0 - 0.5) * 0.02 * CELL_DEG;
        posGridHold(&h, v, 16.1, &a, &b);
        long c = cell(a);
        if (c != last) { changes++; last = c; }
        long r = cell(v);
        if (r != ref_last) { ref_changes++; ref_last = r; }
    }
    printf("jitter: hold changes=%ld reference changes=%ld\n", changes, ref_changes);
    TEST_ASSERT_EQUAL_INT32(0, changes);
    TEST_ASSERT_TRUE(ref_changes > 100);
}

void test_reset(void)
{
    struct PosGridHold h; double a, b;
    posGridHoldReset(&h);
    posGridHold(&h, 48.0083, 16.1, &a, &b);
    posGridHoldReset(&h);
    TEST_ASSERT_FALSE(h.init);
    posGridHold(&h, 50.5, 10.5, &a, &b);   // seeds again, no hold against old value
    TEST_ASSERT_TRUE(fabs(a - 50.5) < 1e-12);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_cell_hand_values);
    RUN_TEST(test_first_call_emits_input);
    RUN_TEST(test_hold_within_cell);
    RUN_TEST(test_hold_across_border_in_band);
    RUN_TEST(test_release_beyond_band);
    RUN_TEST(test_axes_independent);
    RUN_TEST(test_south_west);
    RUN_TEST(test_degree_rollover);
    RUN_TEST(test_jitter_no_cell_change);
    RUN_TEST(test_reset);
    return UNITY_END();
}
