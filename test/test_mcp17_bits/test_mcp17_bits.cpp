// Native Testsuite fuer mcp17PortABits() -- formatiert die MCP23017 Port-A
// Eingaenge als 8-Zeichen '0'/'1'-String, geteilt vom Positionsbeacon (/D=,
// PositionToAPRS()) und dem digitalen Slot des APRS T# Telemetrierahmens
// (sendTelemetry(), upstream issue 1076). Siehe src/mcp17_bits.h.
//
//   pio test -e native -f test_mcp17_bits

#include <unity.h>
#include <string.h>

#include <mcp17_bits.h>

void setUp(void) {}
void tearDown(void) {}

// Alle Eingaenge low, kein Pin als OUTPUT markiert.
static void test_all_zero_yields_all_zero_string(void)
{
    char out[MCP17_BITS_LEN + 1];
    memset(out, 0xAA, sizeof(out));
    mcp17PortABits(0x0000, 0x0000, out);
    TEST_ASSERT_EQUAL_STRING("00000000", out);
    TEST_ASSERT_EQUAL_INT(0, out[MCP17_BITS_LEN]);
}

// GPA0 high beweist die Bit-Reihenfolge: out[0] == GPA0.
static void test_gpa0_high_sets_first_char(void)
{
    char out[MCP17_BITS_LEN + 1];
    mcp17PortABits(0x0001, 0x0000, out);
    TEST_ASSERT_EQUAL_STRING("10000000", out);
}

// GPA7 high setzt das letzte Zeichen.
static void test_gpa7_high_sets_last_char(void)
{
    char out[MCP17_BITS_LEN + 1];
    mcp17PortABits(0x0080, 0x0000, out);
    TEST_ASSERT_EQUAL_STRING("00000001", out);
}

// Alle Port-A Pins high, keiner als OUTPUT konfiguriert.
static void test_all_port_a_high_no_outputs(void)
{
    char out[MCP17_BITS_LEN + 1];
    mcp17PortABits(0x00FF, 0x0000, out);
    TEST_ASSERT_EQUAL_STRING("11111111", out);
}

// GPA0 und GPA2 sind als OUTPUT markiert -- ihr Eingangswert wird durch '0'
// ersetzt, damit ein veralteter Eingangswert nach --setio nicht in den
// Rahmen durchsickert.
static void test_output_pins_are_forced_to_zero(void)
{
    char out[MCP17_BITS_LEN + 1];
    mcp17PortABits(0x00FF, 0x0005, out);
    TEST_ASSERT_EQUAL_STRING("01011111", out);
}

// Port B (Bit 8-15) wird komplett ignoriert.
static void test_port_b_bits_are_ignored(void)
{
    char out[MCP17_BITS_LEN + 1];
    mcp17PortABits(0xFF00, 0x0000, out);
    TEST_ASSERT_EQUAL_STRING("00000000", out);
}

// Ein io_mask, der nur Port-B-Bits setzt, hat keinerlei Wirkung auf Port A.
static void test_port_b_mask_bits_are_irrelevant(void)
{
    char out[MCP17_BITS_LEN + 1];
    mcp17PortABits(0xFFFF, 0xFF00, out);
    TEST_ASSERT_EQUAL_STRING("11111111", out);
}

// Gemischtes Bitmuster ohne Outputs.
static void test_mixed_pattern(void)
{
    char out[MCP17_BITS_LEN + 1];
    mcp17PortABits(0x00A5, 0x0000, out);
    TEST_ASSERT_EQUAL_STRING("10100101", out);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_zero_yields_all_zero_string);
    RUN_TEST(test_gpa0_high_sets_first_char);
    RUN_TEST(test_gpa7_high_sets_last_char);
    RUN_TEST(test_all_port_a_high_no_outputs);
    RUN_TEST(test_output_pins_are_forced_to_zero);
    RUN_TEST(test_port_b_bits_are_ignored);
    RUN_TEST(test_port_b_mask_bits_are_irrelevant);
    RUN_TEST(test_mixed_pattern);
    return UNITY_END();
}
