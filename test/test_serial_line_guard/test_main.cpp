// test_serial_line_guard: serialLineIsClean() (src/serial_line_guard.h), SER-JUNK-01. A typed console line passes,
// supply-noise junk does not.
#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <serial_line_guard.h>

void setUp(void) {}
void tearDown(void) {}

static bool clean(const char *s) { return serialLineIsClean(s, strlen(s)); }
static bool cleanN(const char *s, size_t n) { return serialLineIsClean(s, n); }

static void test_typed_lines_pass(void)
{
    TEST_ASSERT_TRUE(clean("--info\r"));
    TEST_ASSERT_TRUE(clean("--info\r\n"));
    TEST_ASSERT_TRUE(clean("::9 characterization test\r"));
    TEST_ASSERT_TRUE(clean("::{DK5EN-1}hallo du\n"));
    TEST_ASSERT_TRUE(clean("::9 helllo\x08\x08o\r"));        // backspace editing
    TEST_ASSERT_TRUE(clean("::9 a\x7F" "b\r"));               // DEL
    TEST_ASSERT_TRUE(clean("--setname\tx\r"));                // TAB
    TEST_ASSERT_TRUE(clean("::9 Gr\xC3\xBC\xC3\x9F" "e \xE2\x82\xAC 5\r"));   // ü, ß, euro sign
    TEST_ASSERT_TRUE(clean("::9 \xF0\x9F\x98\x80\r"));       // emoji, 4-byte UTF-8
    TEST_ASSERT_TRUE(clean(""));
}

static void test_noise_is_dropped(void)
{
    TEST_ASSERT_FALSE(clean("::\x8F\x13zz\r"));               // lone continuation byte, control byte
    TEST_ASSERT_FALSE(clean("--\x01reboot\r"));               // control byte inside a command
    TEST_ASSERT_FALSE(clean("::9 \x1B[Atext\r"));             // ESC (cursor keys are not text either)
    TEST_ASSERT_FALSE(clean("::9 \xC0\xAF\r"));               // overlong '/'
    TEST_ASSERT_FALSE(clean("::9 \xC1\xBF\r"));
    TEST_ASSERT_FALSE(clean("::9 \xE0\x80\xAF\r"));           // overlong 3-byte
    TEST_ASSERT_FALSE(clean("::9 \xED\xA0\x80\r"));           // surrogate
    TEST_ASSERT_FALSE(clean("::9 \xF4\x90\x80\x80\r"));       // above U+10FFFF
    TEST_ASSERT_FALSE(clean("::9 \xF5\x80\x80\x80\r"));
    TEST_ASSERT_FALSE(clean("::9 \xFF\r"));
    TEST_ASSERT_FALSE(cleanN("::9 \xC3", 5));                 // sequence cut at the end
    TEST_ASSERT_FALSE(clean("::9 \xE2\x82\r"));               // sequence cut by CR
    TEST_ASSERT_FALSE(serialLineIsClean(nullptr, 3));
}

// Random noise: a line of 8 or more random bytes is practically never clean (each byte is clean with about
// 99/256), so junk with a "::" or "--" start almost never survives.
static void test_random_noise(void)
{
    uint32_t s = 12345u;
    unsigned passed = 0, total = 0;
    char buf[64];
    for (int it = 0; it < 200000; it++)
    {
        size_t n = 8 + (it % 40);
        for (size_t k = 0; k < n; k++) { s = s * 1664525u + 1013904223u; buf[k] = (char)(s >> 24); }
        total++;
        if (cleanN(buf, n)) passed++;
    }
    TEST_ASSERT_LESS_THAN_UINT32(total / 1000, passed);       // below 0.1 %
}

// TX gate: read while idle, hold during TX, flush exactly once when TX ends, then read again.
static void test_tx_gate(void)
{
    bool was = false;
    TEST_ASSERT_EQUAL(SERIAL_TX_READ, serialTxGateStep(false, &was));
    TEST_ASSERT_EQUAL(SERIAL_TX_HOLD, serialTxGateStep(true, &was));
    TEST_ASSERT_EQUAL(SERIAL_TX_HOLD, serialTxGateStep(true, &was));
    TEST_ASSERT_EQUAL(SERIAL_TX_FLUSH, serialTxGateStep(false, &was));
    TEST_ASSERT_EQUAL(SERIAL_TX_READ, serialTxGateStep(false, &was));
    TEST_ASSERT_EQUAL(SERIAL_TX_HOLD, serialTxGateStep(true, &was));   // next TX
    TEST_ASSERT_EQUAL(SERIAL_TX_FLUSH, serialTxGateStep(false, &was));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_typed_lines_pass);
    RUN_TEST(test_noise_is_dropped);
    RUN_TEST(test_random_noise);
    RUN_TEST(test_tx_gate);
    return UNITY_END();
}
