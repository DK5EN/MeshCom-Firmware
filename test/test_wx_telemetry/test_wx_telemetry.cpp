// WX-01 (upstream #999): T# value formatting for uvindex / wind / gust.
#include <unity.h>
#include <string.h>
#include "wx_telemetry.h"

void setUp(void) {}
void tearDown(void) {}

static wx_mean_t uv, wind;
static wx_gust_t gust;

static void clear_all(void)
{
    wxMeanReset(&uv); wxMeanReset(&wind); wxGustReset(&gust);
}

void test_values(void)
{
    clear_all();
    wxMeanAdd(&uv, 3.0f); wxMeanAdd(&uv, 4.0f);
    wxMeanAdd(&wind, 10.0f); wxMeanAdd(&wind, 11.0f); wxMeanAdd(&wind, 12.0f);
    wxGustAdd(&gust, 10.0f); wxGustAdd(&gust, 20.0f); wxGustAdd(&gust, 30.0f);
    char b[16];
    TEST_ASSERT_TRUE(wxTelemetryValue("uvindex", &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("3.5", b);
    TEST_ASSERT_TRUE(wxTelemetryValue("wind", &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("11.0", b);
    TEST_ASSERT_TRUE(wxTelemetryValue("gust", &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("20.0", b);
}

void test_empty_window(void)
{
    clear_all();
    char b[16] = "xx";
    TEST_ASSERT_FALSE(wxTelemetryValue("uvindex", &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("", b);
    strcpy(b, "xx");
    TEST_ASSERT_FALSE(wxTelemetryValue("wind", &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("", b);
    strcpy(b, "xx");
    wxGustAdd(&gust, 5.0f); wxGustAdd(&gust, 5.0f);  // only 2 samples: no gust yet
    TEST_ASSERT_FALSE(wxTelemetryValue("gust", &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("", b);
}

void test_unknown_and_null(void)
{
    clear_all();
    wxMeanAdd(&uv, 1.0f);
    char b[16] = "xx";
    TEST_ASSERT_FALSE(wxTelemetryValue("temp", &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("", b);
    TEST_ASSERT_FALSE(wxTelemetryValue(NULL, &uv, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_FALSE(wxTelemetryValue("uvindex", NULL, &wind, &gust, b, sizeof(b)));
    TEST_ASSERT_FALSE(wxTelemetryValue("uvindex", &uv, &wind, &gust, NULL, 0));
}

void test_truncation(void)
{
    clear_all();
    wxMeanAdd(&wind, 123.45f);
    char b[4];
    memset(b, 'z', sizeof(b));
    wxTelemetryValue("wind", &uv, &wind, &gust, b, sizeof(b));
    TEST_ASSERT_EQUAL(3, (int)strlen(b));  // "123", NUL-terminated
    char c[2] = {'z', 'z'};
    wxTelemetryValue("wind", &uv, &wind, &gust, c, 1);
    TEST_ASSERT_EQUAL('\0', c[0]);
    TEST_ASSERT_EQUAL('z', c[1]);
}

void test_debug_line(void)
{
    clear_all();
    char b[96];
    wxTelemetryDebugLine(&uv, &wind, &gust, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("[WX] T# uvindex=- (n=0) wind=- (n=0) gust=-", b);
    wxMeanAdd(&uv, 2.0f);
    wxMeanAdd(&wind, 8.0f); wxMeanAdd(&wind, 9.0f);
    wxGustAdd(&gust, 8.0f); wxGustAdd(&gust, 9.0f); wxGustAdd(&gust, 10.0f);
    wxTelemetryDebugLine(&uv, &wind, &gust, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("[WX] T# uvindex=2.0 (n=1) wind=8.5 (n=2) gust=9.0", b);
    char s[10];
    wxTelemetryDebugLine(&uv, &wind, &gust, s, sizeof(s));
    TEST_ASSERT_EQUAL(9, (int)strlen(s));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_values);
    RUN_TEST(test_empty_window);
    RUN_TEST(test_unknown_and_null);
    RUN_TEST(test_truncation);
    RUN_TEST(test_debug_line);
    return UNITY_END();
}
