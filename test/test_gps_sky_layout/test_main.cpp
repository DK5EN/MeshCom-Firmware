// Position tab layout: SKY/TEXT side tabs attached to the card, SEND POS clear of the tabs.
#include <unity.h>
#include "t-deck/tdeck_gps_sky_layout.h"

using namespace skylayout;

void setUp(void) {}
void tearDown(void) {}

static bool overlap(const Rect &a, const Rect &b)
{
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

static void test_tab_nose_overlaps_card_border(void)
{
    const Rect tabs[2] = {kTabSky, kTabText};
    for(const Rect &t : tabs)
    {
        TEST_ASSERT_TRUE_MESSAGE(t.x >= kCard.right() - kBorder && t.x < kCard.right(), "tab left edge not in card right border");
        TEST_ASSERT_TRUE_MESSAGE(t.y >= kCard.y && t.bottom() <= kCard.bottom(), "tab not beside the card");
    }
    TEST_ASSERT_TRUE(kTabInactiveDx >= kBorder + 2 && kTabInactiveDx <= kBorder + 3);   // 2-3 px gap when inactive
}

static void test_tab_size_and_gap(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(28, kTabSky.h);
    TEST_ASSERT_GREATER_OR_EQUAL(28, kTabText.h);
    TEST_ASSERT_TRUE(kTabSky.w >= 56 && kTabSky.w <= 64);
    TEST_ASSERT_GREATER_OR_EQUAL(8, kTabText.y - kTabSky.bottom());
}

static void test_sendpos_clear_of_tabs(void)
{
    TEST_ASSERT_GREATER_OR_EQUAL(28, kSendPos.h);
    TEST_ASSERT_GREATER_OR_EQUAL(kTabText.bottom() + 24, kSendPos.y);
    TEST_ASSERT_TRUE(kInfo.y >= kTabText.bottom() && kInfo.bottom() <= kSendPos.y);   // info between
}

static void test_inside_panel(void)
{
    const Rect all[5] = {kCard, kTabSky, kTabText, kSendPos, kInfo};
    for(const Rect &r : all)
    {
        TEST_ASSERT_GREATER_OR_EQUAL(0, r.x);
        TEST_ASSERT_GREATER_OR_EQUAL(0, r.y);
        TEST_ASSERT_LESS_OR_EQUAL(kMaxX, r.right());
        TEST_ASSERT_LESS_OR_EQUAL(kPageH, r.bottom());
    }
}

static void test_no_overlap_except_nose(void)
{
    const Rect all[5] = {kCard, kTabSky, kTabText, kSendPos, kInfo};
    for(int i = 0; i < 5; i++)
        for(int j = i + 1; j < 5; j++)
        {
            if(i == 0 && (j == 1 || j == 2))
                continue;   // intended: nose over the card border
            TEST_ASSERT_FALSE_MESSAGE(overlap(all[i], all[j]), "unintended rect overlap");
        }
}

static void test_plot_fits_card(void)
{
    TEST_ASSERT_TRUE(kCx - kR - 14 >= kBorder && kCx + kR + 14 <= kCard.w - kBorder);   // W/E labels
    TEST_ASSERT_TRUE(kCy - kR - 12 >= kBorder && kCy + kR + 12 <= kCard.h - kBorder);   // N/S labels
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_tab_nose_overlaps_card_border);
    RUN_TEST(test_tab_size_and_gap);
    RUN_TEST(test_sendpos_clear_of_tabs);
    RUN_TEST(test_inside_panel);
    RUN_TEST(test_no_overlap_except_nose);
    RUN_TEST(test_plot_fits_card);
    return UNITY_END();
}
