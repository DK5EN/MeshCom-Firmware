// TDECK-VOLT-01 (#1312) -- src/batt_label.h: OLED equivalence, T-Deck labels, source pins.
#include <unity.h>
#include "../support/repo_file.h"
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <sstream>
#include <string>
#include <batt_label.h>

void setUp(void) {}
void tearDown(void) {}

static const int kPct[] = {-5, 0, 7, 42, 100, 101, 999};

template <typename F> static void for_grid(F f)
{
    for (int i = 0; i <= 435; i++)
        for (int p : kPct) f(i / 100.0, p);
    const double extra[] = {9.99, 10.0, 12.6};
    for (double v : extra)
        for (int p : kPct) f(v, p);
}

static void check_oled(int width, int dec, const char *usbText)
{
    for_grid([&](double v, int p) {
        char want[16], got[16];
        // legacy literal formats, per layout
        if (width == 4) {
            snprintf(want, sizeof want, "%4.1fV", v);
            batt_label_format(got, sizeof got, v, p, true, false, 4, 1, usbText, false);
            TEST_ASSERT_EQUAL_STRING(want, got);
            snprintf(want, sizeof want, "%4d%%", p);
        } else {
            snprintf(want, sizeof want, "%5.2fV", v);
            batt_label_format(got, sizeof got, v, p, true, false, 5, 2, usbText, false);
            TEST_ASSERT_EQUAL_STRING(want, got);
            snprintf(want, sizeof want, "%5d%%", p);
        }
        batt_label_format(got, sizeof got, v, p, false, false, width, dec, usbText, false);
        TEST_ASSERT_EQUAL_STRING(want, got);
    });
    char got[16];
    batt_label_format(got, sizeof got, 0.0, 42, true, true, width, dec, usbText, false);
    TEST_ASSERT_EQUAL_STRING(usbText, got);
}

static void test_oled_tft_layout(void) { check_oled(4, 1, " USB"); }
static void test_oled_plain_layout(void) { check_oled(5, 2, "   USB"); }

static void test_tdeck_labels(void)
{
    char b[8];
    batt_label_format(b, sizeof b, 3.87, 42, true, false, 0, 1, "USB", true);
    TEST_ASSERT_EQUAL_STRING("3.9V", b);
    batt_label_format(b, sizeof b, 4.2, 42, true, false, 0, 1, "USB", true);
    TEST_ASSERT_EQUAL_STRING("4.2V", b);
    batt_label_format(b, sizeof b, 3.87, 42, false, false, 0, 1, "USB", true);
    TEST_ASSERT_EQUAL_STRING("42%", b);
    batt_label_format(b, sizeof b, 3.87, 101, false, false, 0, 1, "USB", true);
    TEST_ASSERT_EQUAL_STRING("100%", b);
    batt_label_format(b, sizeof b, 3.87, -5, false, false, 0, 1, "USB", true);
    TEST_ASSERT_EQUAL_STRING("0%", b);
    batt_label_format(b, sizeof b, 0.0, 42, true, true, 0, 1, "USB", true);
    TEST_ASSERT_EQUAL_STRING("USB", b);
}

static void test_tdeck_buffer_8_no_truncation(void)
{
    char b[8];
    // longest T-Deck labels: "100%", "4.2V", "USB"; even 4-digit volts "12.6V" fits
    TEST_ASSERT_LESS_THAN(8, batt_label_format(b, sizeof b, 12.6, 100, true, false, 0, 1, "USB", true));
    TEST_ASSERT_LESS_THAN(8, batt_label_format(b, sizeof b, 3.9, 100, false, false, 0, 1, "USB", true));
    TEST_ASSERT_LESS_THAN(8, batt_label_format(b, sizeof b, 3.9, 9999, false, false, 0, 1, "USB", true));
}

static std::string slurp(const char *rel)
{
    return repo_slurp(rel);
}

static std::string func_body(const std::string &src, const char *sig)
{
    size_t a = src.find(sig);
    TEST_ASSERT_TRUE_MESSAGE(a != std::string::npos, sig);
    size_t b = src.find("\n}\n", a);
    TEST_ASSERT_TRUE_MESSAGE(b != std::string::npos, "function end");
    return src.substr(a, b - a);
}

static bool has(const std::string &s, const char *n) { return s.find(n) != std::string::npos; }

static void test_pin_tdeck_header_uses_helper(void)
{
    std::string body = func_body(slurp("src/t-deck/lv_obj_functions.cpp"),
                                 "static void update_header_batt_indicator(float batt, int proz)\n{");
    TEST_ASSERT_TRUE_MESSAGE(has(body, "bDisplayVolt"), "header must read bDisplayVolt");
    TEST_ASSERT_TRUE_MESSAGE(has(body, "batt_label_format("), "header must call the helper");
    TEST_ASSERT_FALSE_MESSAGE(has(body, "\"%d%%\""), "no inline percent format");
}

static void test_pin_oled_sites_use_helper(void)
{
    // RED until the orchestrator applies the loop_functions.cpp patch requests.
    std::string src = slurp("src/loop_functions.cpp");
    const char *sigs[] = {"static void sendDisplayClockLine()\n{", "void sendDisplayMainline()\n{",
                          "void wpRefreshClock("};
    for (const char *s : sigs) {
        std::string body = func_body(src, s);
        TEST_ASSERT_TRUE_MESSAGE(has(body, "batt_label_format("), s);
        TEST_ASSERT_FALSE_MESSAGE(has(body, "\"%5.2fV\""), s);
        TEST_ASSERT_FALSE_MESSAGE(has(body, "\"%4.1fV\""), s);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_oled_tft_layout);
    RUN_TEST(test_oled_plain_layout);
    RUN_TEST(test_tdeck_labels);
    RUN_TEST(test_tdeck_buffer_8_no_truncation);
    RUN_TEST(test_pin_tdeck_header_uses_helper);
    RUN_TEST(test_pin_oled_sites_use_helper);
    return UNITY_END();
}
