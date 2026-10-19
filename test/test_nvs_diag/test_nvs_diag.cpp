// RM-NVS-01: the NVS diagnostic line formatter (src/nvs_diag.h).
#include <unity.h>
#include <string.h>
#include <nvs_diag.h>

void setUp() {}
void tearDown() {}

static void test_normal()
{
    char b[96];
    NvsDiagStats s = {12, 8, 20, 5};
    nvsDiagFormat(b, sizeof(b), s);
    TEST_ASSERT_EQUAL_STRING("[NVS];used;12;free;8;total;20;ns;5", b);
    nvsDiagFormatInfo(b, sizeof(b), s);
    TEST_ASSERT_EQUAL_STRING("used 12 free 8 total 20 ns 5", b);
}

static void test_zero()
{
    char b[96];
    NvsDiagStats s = {0, 0, 0, 0};
    nvsDiagFormat(b, sizeof(b), s);
    TEST_ASSERT_EQUAL_STRING("[NVS];used;0;free;0;total;0;ns;0", b);
}

static void test_large()
{
    char b[96];
    NvsDiagStats s = {4294967295u, 4294967295u, 4294967295u, 4294967295u};
    nvsDiagFormat(b, sizeof(b), s);
    TEST_ASSERT_EQUAL_STRING("[NVS];used;4294967295;free;4294967295;total;4294967295;ns;4294967295", b);
}

static void test_truncation()
{
    NvsDiagStats s = {12, 8, 20, 5};
    char b[12 + 4];
    memset(b, 'X', sizeof(b));
    nvsDiagFormat(b, 12, s);
    TEST_ASSERT_EQUAL_STRING("[NVS];used;", b); // 11 chars + NUL
    TEST_ASSERT_EQUAL_CHAR('X', b[12]);         // nothing written past cap
    TEST_ASSERT_EQUAL_CHAR('X', b[15]);

    char one[2] = {'X', 'X'};
    nvsDiagFormat(one, 1, s);
    TEST_ASSERT_EQUAL_CHAR('\0', one[0]);
    TEST_ASSERT_EQUAL_CHAR('X', one[1]);

    nvsDiagFormat(one, 0, s); // cap 0: untouched
    TEST_ASSERT_EQUAL_CHAR('\0', one[0]);

    char i[8];
    nvsDiagFormatInfo(i, sizeof(i), s);
    TEST_ASSERT_EQUAL_UINT(7, strlen(i));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_normal);
    RUN_TEST(test_zero);
    RUN_TEST(test_large);
    RUN_TEST(test_truncation);
    return UNITY_END();
}
