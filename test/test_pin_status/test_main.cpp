// Unity tests of src/pin_status_json.h: GET /pinstatus document. Host only; there is no MCP23017 on the bench, the
// expander side is proven here with a plain struct, not with a chip.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include "../../src/pin_status_json.h"

void setUp(void) {}
void tearDown(void) {}

// ---- minimal strict JSON validator (RFC 8259 subset: all we emit) ----
struct V { const char *p; };
static void ws(V &v) { while (*v.p == ' ' || *v.p == '\n' || *v.p == '\t' || *v.p == '\r') v.p++; }
static bool val(V &v);
static bool str(V &v)
{
    if (*v.p != '"') return false;
    v.p++;
    while (*v.p && *v.p != '"')
    {
        unsigned char c = (unsigned char)*v.p;
        if (c < 0x20) return false;                       // raw control char
        if (c == '\\')
        {
            v.p++;
            if (strchr("\"\\/bfnrt", *v.p) && *v.p) { v.p++; continue; }
            if (*v.p == 'u') { v.p++; for (int i = 0; i < 4; i++, v.p++) if (!strchr("0123456789abcdefABCDEF", *v.p) || !*v.p) return false; continue; }
            return false;
        }
        v.p++;
    }
    if (*v.p != '"') return false;
    v.p++;
    return true;
}
static bool val(V &v)
{
    ws(v);
    if (*v.p == '"') return str(v);
    if (*v.p == '{')
    {
        v.p++; ws(v);
        if (*v.p == '}') { v.p++; return true; }
        for (;;)
        {
            ws(v);
            if (!str(v)) return false;
            ws(v);
            if (*v.p++ != ':') return false;
            if (!val(v)) return false;
            ws(v);
            if (*v.p == ',') { v.p++; continue; }
            if (*v.p == '}') { v.p++; return true; }
            return false;
        }
    }
    if (*v.p == '[')
    {
        v.p++; ws(v);
        if (*v.p == ']') { v.p++; return true; }
        for (;;)
        {
            if (!val(v)) return false;
            ws(v);
            if (*v.p == ',') { v.p++; continue; }
            if (*v.p == ']') { v.p++; return true; }
            return false;
        }
    }
    if (*v.p >= '0' && *v.p <= '9') { while (*v.p >= '0' && *v.p <= '9') v.p++; return true; }
    return false;
}
static bool validJson(const char *s)
{
    V v = {s};
    if (!val(v)) return false;
    ws(v);
    return *v.p == 0;
}

static char g_nm[PIN_COUNT][PIN_NAME_MAX];   // the names the PinStatusIn points at (the route points at the settings)

static PinStatusIn base(void)
{
    PinStatusIn in;
    memset(&in, 0, sizeof(in));
    memset(g_nm, 0, sizeof(g_nm));
    in.names = g_nm;
    in.chgMode = "off";
    return in;
}

static char g_buf[PIN_STATUS_JSON_MAX];

static void test_all_absent(void)
{
    PinStatusIn in = base();
    size_t n = pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_EQUAL_UINT32(strlen(g_buf), n);
    TEST_ASSERT_TRUE(validJson(g_buf));
    TEST_ASSERT_EQUAL_STRING(
        "{\"led\":{\"fitted\":0,\"mode\":\"off\",\"busy\":0,\"rm\":\"\",\"state\":\"off\"},"
        "\"chg\":{\"fitted\":0,\"mode\":\"off\",\"eff\":\"off\",\"rm\":0},"
        "\"mcp\":{\"present\":0,\"pins\":[]}}", g_buf);
}

static void test_led_modes(void)
{
    PinStatusIn in = base();
    in.ledFitted = true;
    const struct { int mode; const char *name; } m[3] = {{PIN_LED_OFF, "off"}, {PIN_LED_FLASH, "flash"}, {PIN_LED_STEADY, "steady"}};
    for (int i = 0; i < 3; i++)
    {
        in.ledMode = m[i].mode;
        TEST_ASSERT_TRUE(pinStatusJson(in, g_buf, sizeof(g_buf)) > 0);
        TEST_ASSERT_TRUE(validJson(g_buf));
        char want[64];
        snprintf(want, sizeof(want), "\"fitted\":1,\"mode\":\"%s\",\"busy\":0,\"rm\":\"\",\"state\":\"%s\"", m[i].name, m[i].name);
        TEST_ASSERT_NOT_NULL(strstr(g_buf, want));
    }
    in.ledMode = 99;   // unknown value reads as off
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"mode\":\"off\""));
}

static void test_led_busy_and_rm(void)
{
    PinStatusIn in = base();
    in.ledFitted = true;
    in.ledMode = PIN_LED_STEADY;
    in.ledBusy = true;   // OneWire owns the pad: the stored mode cannot run
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"mode\":\"steady\",\"busy\":1,\"rm\":\"\",\"state\":\"off\""));
    in.ledBusy = false;
    in.ledMode = PIN_LED_OFF;
    in.ledRm = PIN_RM_ON;   // RM1 hold wins over the stored mode
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"mode\":\"off\",\"busy\":0,\"rm\":\"on\",\"state\":\"steady\""));
    in.ledRm = PIN_RM_FLASH;
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"rm\":\"flash\",\"state\":\"flash\""));
    in.ledBusy = true;   // busy beats a hold
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"rm\":\"flash\",\"state\":\"off\""));
}

static void test_charge_led(void)
{
    PinStatusIn in = base();
    in.chgFitted = true;
    in.chgMode = "auto";
    in.chgEff = "off";
    in.chgRm = true;
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_TRUE(validJson(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"chg\":{\"fitted\":1,\"mode\":\"auto\",\"eff\":\"off\",\"rm\":1}"));
    in.chgEff = "x\"y";   // only known words come out
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"eff\":\"off\""));
    in.chgEff = NULL;
    in.chgMode = NULL;
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"mode\":\"off\",\"eff\":\"off\""));
}

static int countStr(const char *s, const char *needle)
{
    int c = 0;
    for (const char *p = s; (p = strstr(p, needle)); p += strlen(needle)) c++;
    return c;
}

static void test_mcp_present(void)
{
    PinStatusIn in = base();
    in.mcpPresent = true;
    in.mcpIo = 0x8001;      // A0 and B7 are outputs
    in.mcpOut = 0x8000;     // B7 high, A0 low
    in.mcpIn = 0x0006;      // A1, A2 read high
    snprintf(g_nm[0], PIN_NAME_MAX, "Relay");
    snprintf(g_nm[15], PIN_NAME_MAX, "Pump");
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_TRUE(validJson(g_buf));
    TEST_ASSERT_EQUAL_INT(16, countStr(g_buf, "{\"id\":\""));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"mcp\":{\"present\":1,\"pins\":[{\"id\":\"A0\",\"n\":\"Relay\",\"d\":\"out\",\"l\":0}"));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "{\"id\":\"A1\",\"n\":\"\",\"d\":\"in\",\"l\":1}"));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "{\"id\":\"A2\",\"n\":\"\",\"d\":\"in\",\"l\":1}"));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "{\"id\":\"A3\",\"n\":\"\",\"d\":\"in\",\"l\":0}"));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "{\"id\":\"B0\""));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "{\"id\":\"B7\",\"n\":\"Pump\",\"d\":\"out\",\"l\":1}"));
}

static void test_mcp_out_uses_out_bit_in_uses_in_bit(void)
{
    PinStatusIn in = base();
    in.mcpPresent = true;
    in.mcpIo = 0x0001;
    in.mcpOut = 0x0000;
    in.mcpIn = 0xFFFF;   // an output pin ignores the input sample, an input pin ignores the out bit
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "{\"id\":\"A0\",\"n\":\"\",\"d\":\"out\",\"l\":0}"));
    in.mcpIo = 0;
    in.mcpOut = 0xFFFF;
    in.mcpIn = 0;
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "{\"id\":\"A0\",\"n\":\"\",\"d\":\"in\",\"l\":0}"));
}

static void test_mcp_absent_has_no_pins_even_with_bits(void)
{
    PinStatusIn in = base();
    in.mcpPresent = false;
    in.mcpIo = 0xFFFF;
    in.mcpOut = 0xFFFF;
    snprintf(g_nm[3], PIN_NAME_MAX, "ghost");
    pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_TRUE(validJson(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"mcp\":{\"present\":0,\"pins\":[]}"));
    TEST_ASSERT_NULL(strstr(g_buf, "ghost"));
}

static void nameCase(const char *raw, const char *wantEscaped)
{
    PinStatusIn in = base();
    in.mcpPresent = true;
    memset(g_nm[0], 0, PIN_NAME_MAX);
    strncpy(g_nm[0], raw, PIN_NAME_MAX - 1);
    size_t n = pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE_MESSAGE(validJson(g_buf), g_buf);
    char want[160];
    snprintf(want, sizeof(want), "{\"id\":\"A0\",\"n\":\"%s\",\"d\":\"in\"", wantEscaped);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_buf, want), g_buf);
}

static void test_name_escaping(void)
{
    nameCase("a\"b", "a\\\"b");
    nameCase("a\\b", "a\\\\b");
    nameCase("\\\"", "\\\\\\\"");
    nameCase("l1\nl2\r\t", "l1\\nl2\\r\\t");
    nameCase("\x01\x1f", "\\u0001\\u001f");
    nameCase("\x7f", "\\u007f");
    nameCase("</script>", "</script>");
    // 15 control characters: the longest a name can get (6 bytes each) still fits the document
    nameCase("\x01\x02\x03\x04\x05\x06\x07\x0b\x0c\x0e\x0f\x10\x11\x12\x13",
             "\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u000b\\u000c\\u000e\\u000f\\u0010\\u0011\\u0012\\u0013");
}

static void test_name_utf8_and_latin1(void)
{
    nameCase("Pumpe \xC3\xA4\xC3\xB6\xC3\xBC", "Pumpe \xC3\xA4\xC3\xB6\xC3\xBC");   // UTF-8 stays as it is
    nameCase("\xE2\x82\xAC", "\xE2\x82\xAC");                                      // 3-byte
    nameCase("\xF0\x9F\x92\xA1", "\xF0\x9F\x92\xA1");                              // 4-byte
    nameCase("Pumpe \xE4", "Pumpe \\u00e4");                                       // lone Latin-1 byte
    nameCase("\xC3", "\\u00c3");                                                    // cut-off sequence
    nameCase("\xC0\x80", "\\u00c0\\u0080");                                         // overlong
    nameCase("\xED\xA0\x80", "\\u00ed\\u00a0\\u0080");                              // surrogate
    nameCase("\xFF", "\\u00ff");
}

static void test_name_unterminated_is_bounded(void)
{
    PinStatusIn in = base();
    in.mcpPresent = true;
    memset(g_nm[0], 'x', PIN_NAME_MAX);   // no NUL at all: read stops at 15
    size_t n = pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(validJson(g_buf));
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"n\":\"xxxxxxxxxxxxxxx\",\"d\""));
}

static void test_worst_case_fits_max(void)
{
    PinStatusIn in = base();
    in.ledFitted = in.chgFitted = in.mcpPresent = true;
    in.ledMode = PIN_LED_STEADY;
    in.ledRm = PIN_RM_FLASH;
    in.chgMode = "auto";
    in.chgEff = "auto";
    in.mcpIo = 0xFFFF;
    for (int i = 0; i < PIN_COUNT; i++)
        for (int k = 0; k < PIN_NAME_MAX - 1; k++)
            g_nm[i][k] = (char)(1 + (k % 6));   // \u0001..\u0006
    size_t n = pinStatusJson(in, g_buf, sizeof(g_buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_TRUE(n < PIN_STATUS_JSON_MAX);
    TEST_ASSERT_TRUE(validJson(g_buf));
    // pinned: the true worst case is 2156 bytes (+ NUL); the buffer must stay close to it, it lives on a task stack
    TEST_ASSERT_EQUAL_UINT32(2156, n);
    TEST_ASSERT_TRUE(PIN_STATUS_JSON_MAX >= 2156 + 1);
    TEST_ASSERT_TRUE(PIN_STATUS_JSON_MAX <= 2156 + 64);
    char exact[PIN_STATUS_JSON_MAX];
    TEST_ASSERT_EQUAL_UINT32(n, pinStatusJson(in, exact, n + 1));   // fits with exactly the NUL spare
    TEST_ASSERT_EQUAL_UINT32(0, pinStatusJson(in, exact, n));

    // and it really is the largest: every combination of the discrete fields, worst-case names, io bits all out/in
    const char *cm[] = {NULL, "off", "auto"};
    const char *ce[] = {NULL, "off", "on", "auto"};
    size_t most = 0;
    for (int lm = 0; lm < 3; lm++)
        for (int busy = 0; busy < 2; busy++)
            for (int rm = 0; rm < 3; rm++)
                for (int c1 = 0; c1 < 3; c1++)
                    for (int c2 = 0; c2 < 4; c2++)
                        for (int crm = 0; crm < 2; crm++)
                            for (int io = 0; io < 2; io++)
                                for (int lv = 0; lv < 2; lv++)
                                {
                                    PinStatusIn w = in;
                                    w.ledMode = lm; w.ledBusy = busy; w.ledRm = rm;
                                    w.chgMode = cm[c1]; w.chgEff = ce[c2]; w.chgRm = crm;
                                    w.mcpIo = io ? 0xFFFF : 0; w.mcpOut = w.mcpIn = lv ? 0xFFFF : 0;
                                    const size_t k = pinStatusJson(w, exact, sizeof(exact));
                                    TEST_ASSERT_TRUE(k > 0);
                                    if (k > most)
                                        most = k;
                                }
    TEST_ASSERT_EQUAL_UINT32(2156, most);
}

static void test_names_are_not_copied(void)
{
    // PinStatusIn holds a pointer, not a copy: a change in the settings array shows up without refilling the struct
    PinStatusIn in = base();
    in.mcpPresent = true;
    in.mcpIo = 0xFFFF;
    snprintf(g_nm[2], PIN_NAME_MAX, "first");
    TEST_ASSERT_TRUE(pinStatusJson(in, g_buf, sizeof(g_buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"n\":\"first\""));
    snprintf(g_nm[2], PIN_NAME_MAX, "second");
    TEST_ASSERT_TRUE(pinStatusJson(in, g_buf, sizeof(g_buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"n\":\"second\""));
    TEST_ASSERT_TRUE(sizeof(PinStatusIn) < 64);   // 16 x 16 bytes of names no longer inside it
    in.names = NULL;   // NULL = all names empty, no crash
    TEST_ASSERT_TRUE(pinStatusJson(in, g_buf, sizeof(g_buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(g_buf, "\"n\":\"\""));
}

static void test_buffer_limit(void)
{
    PinStatusIn in = base();
    in.mcpPresent = true;
    in.ledFitted = true;
    char ok[PIN_STATUS_JSON_MAX];
    size_t full = pinStatusJson(in, ok, sizeof(ok));
    TEST_ASSERT_TRUE(full > 0);
    // exactly full+1 bytes (document + NUL) is enough, one less is not
    char tight[PIN_STATUS_JSON_MAX];
    memset(tight, 'Z', sizeof(tight));
    TEST_ASSERT_EQUAL_UINT32(full, pinStatusJson(in, tight, full + 1));
    TEST_ASSERT_EQUAL_STRING(ok, tight);
    memset(tight, 'Z', sizeof(tight));
    TEST_ASSERT_EQUAL_UINT32(0, pinStatusJson(in, tight, full));
    TEST_ASSERT_EQUAL_CHAR(0, tight[0]);   // never half a document
    TEST_ASSERT_EQUAL_CHAR('Z', tight[full + 1]);   // nothing written past the limit
    // degenerate buffers
    TEST_ASSERT_EQUAL_UINT32(0, pinStatusJson(in, tight, 1));
    TEST_ASSERT_EQUAL_UINT32(0, pinStatusJson(in, tight, 0));
    TEST_ASSERT_EQUAL_UINT32(0, pinStatusJson(in, NULL, 100));
    // every shorter limit gives the same answer: empty, terminated, no overrun
    for (size_t cap = 1; cap < full; cap += 7)
    {
        memset(tight, 'Z', sizeof(tight));
        TEST_ASSERT_EQUAL_UINT32(0, pinStatusJson(in, tight, cap));
        TEST_ASSERT_EQUAL_CHAR(0, tight[0]);
        TEST_ASSERT_EQUAL_CHAR('Z', tight[cap]);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_absent);
    RUN_TEST(test_led_modes);
    RUN_TEST(test_led_busy_and_rm);
    RUN_TEST(test_charge_led);
    RUN_TEST(test_mcp_present);
    RUN_TEST(test_mcp_out_uses_out_bit_in_uses_in_bit);
    RUN_TEST(test_mcp_absent_has_no_pins_even_with_bits);
    RUN_TEST(test_name_escaping);
    RUN_TEST(test_name_utf8_and_latin1);
    RUN_TEST(test_name_unterminated_is_bounded);
    RUN_TEST(test_worst_case_fits_max);
    RUN_TEST(test_names_are_not_copied);
    RUN_TEST(test_buffer_limit);
    return UNITY_END();
}
