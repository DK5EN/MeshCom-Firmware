// posTextFit() / posTextDropMarker() -- the byte budget of the position beacon
// (APRS-TXT-03, upstream issue 1120). PositionToAPRS() drops the comment, then
// the name, when tags + comment + name exceed POS_TEXT_BUDGET (100 bytes).
// D4 B0: the budget stays 100; the drop is now announced by a marker.
//
//   pio test -e native -f test_pos_text_budget

#include <unity.h>

#include <string.h>

#include <pos_text_budget.h>

void setUp(void) {}
void tearDown(void) {}

static void fill(char *dst, size_t n)
{
    memset(dst, 'x', n);
    dst[n] = 0;
}

static void test_budget_is_100(void)
{
    TEST_ASSERT_EQUAL_UINT(100, POS_TEXT_BUDGET);
}

static void test_nothing_dropped_within_budget(void)
{
    char atxt[40], name[24];
    fill(atxt, 25);
    fill(name, 20);
    PosTextFit f = posTextFit(47, atxt, name, POS_TEXT_BUDGET);
    TEST_ASSERT_FALSE(f.atxt_dropped);
    TEST_ASSERT_FALSE(f.name_dropped);
    TEST_ASSERT_EQUAL_UINT(25, strlen(atxt));
    TEST_ASSERT_EQUAL_UINT(20, strlen(name));
}

static void test_exactly_at_budget_keeps_everything(void)
{
    char atxt[40], name[24];
    fill(atxt, 25);
    fill(name, 15);
    PosTextFit f = posTextFit(60, atxt, name, POS_TEXT_BUDGET);  // 100
    TEST_ASSERT_FALSE(f.atxt_dropped);
    TEST_ASSERT_FALSE(f.name_dropped);
}

static void test_one_over_drops_only_the_comment(void)
{
    char atxt[40], name[24];
    fill(atxt, 25);
    fill(name, 15);
    PosTextFit f = posTextFit(61, atxt, name, POS_TEXT_BUDGET);  // 101
    TEST_ASSERT_TRUE(f.atxt_dropped);
    TEST_ASSERT_FALSE(f.name_dropped);
    TEST_ASSERT_EQUAL_UINT(0, strlen(atxt));
    TEST_ASSERT_EQUAL_UINT(15, strlen(name));
}

static void test_reported_weather_node_drops_both(void)
{
    char atxt[40], name[24];
    fill(atxt, 25);
    fill(name, 20);
    PosTextFit f = posTextFit(84, atxt, name, POS_TEXT_BUDGET);  // 129
    TEST_ASSERT_TRUE(f.atxt_dropped);
    TEST_ASSERT_TRUE(f.name_dropped);
    TEST_ASSERT_EQUAL_UINT(0, strlen(atxt));
    TEST_ASSERT_EQUAL_UINT(0, strlen(name));
}

static void test_empty_texts_are_never_reported_as_dropped(void)
{
    char atxt[40] = {0}, name[24] = {0};
    PosTextFit f = posTextFit(120, atxt, name, POS_TEXT_BUDGET);
    TEST_ASSERT_FALSE(f.atxt_dropped);
    TEST_ASSERT_FALSE(f.name_dropped);
}

static void test_marker_text(void)
{
    char out[96];
    PosTextFit a = {true, false};
    PosTextFit n = {false, true};
    PosTextFit b = {true, true};
    PosTextFit none = {false, false};

    posTextDropMarker(out, sizeof(out), a, 84, 100);
    TEST_ASSERT_EQUAL_STRING("[POS];txtdrop;atxt;tags;84;budget;100\n", out);
    posTextDropMarker(out, sizeof(out), n, 84, 100);
    TEST_ASSERT_EQUAL_STRING("[POS];txtdrop;name;tags;84;budget;100\n", out);
    posTextDropMarker(out, sizeof(out), b, 84, 100);
    TEST_ASSERT_EQUAL_STRING("[POS];txtdrop;atxt+name;tags;84;budget;100\n", out);
    TEST_ASSERT_EQUAL_UINT(0, posTextDropMarker(out, sizeof(out), none, 84, 100));
    TEST_ASSERT_EQUAL_STRING("", out);
}

static void test_state_change_reports_once(void)
{
    PosTextFit s = {false, false};
    PosTextFit b = {true, true};
    PosTextFit a = {true, false};
    TEST_ASSERT_FALSE(posTextDropChanged(&s, s));  // no drop, no change
    TEST_ASSERT_TRUE(posTextDropChanged(&s, b));   // first drop
    TEST_ASSERT_FALSE(posTextDropChanged(&s, b));  // same again: silent
    TEST_ASSERT_TRUE(posTextDropChanged(&s, a));   // different outcome
    TEST_ASSERT_TRUE(posTextDropChanged(&s, PosTextFit{false, false}));  // recovered: counts as a change (clears the --info line); the serial marker stays silent
    TEST_ASSERT_FALSE(posTextDropChanged(&s, PosTextFit{false, false}));
}

static void test_info_line_only_while_a_drop_is_active(void)
{
    char out[80];
    posTextDropRecord(PosTextFit{false, false}, 40);
    TEST_ASSERT_EQUAL_UINT(0, posTextInfoLine(out, sizeof(out), 100));
    TEST_ASSERT_EQUAL_STRING("", out);

    TEST_ASSERT_TRUE(posTextDropRecord(PosTextFit{true, true}, 84));
    posTextInfoLine(out, sizeof(out), 100);
    TEST_ASSERT_EQUAL_STRING("APRS text: dropped atxt+name (tags 84, budget 100)", out);

    TEST_ASSERT_TRUE(posTextDropRecord(PosTextFit{true, false}, 61));
    posTextInfoLine(out, sizeof(out), 100);
    TEST_ASSERT_EQUAL_STRING("APRS text: dropped atxt (tags 61, budget 100)", out);

    posTextDropRecord(PosTextFit{false, false}, 40);
    TEST_ASSERT_EQUAL_UINT(0, posTextInfoLine(out, sizeof(out), 100));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_budget_is_100);
    RUN_TEST(test_nothing_dropped_within_budget);
    RUN_TEST(test_exactly_at_budget_keeps_everything);
    RUN_TEST(test_one_over_drops_only_the_comment);
    RUN_TEST(test_reported_weather_node_drops_both);
    RUN_TEST(test_empty_texts_are_never_reported_as_dropped);
    RUN_TEST(test_marker_text);
    RUN_TEST(test_state_change_reports_once);
    RUN_TEST(test_info_line_only_while_a_drop_is_active);
    return UNITY_END();
}
