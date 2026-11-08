// Unity tests of src/pin_status_json.h: GET /pinstatus document. Host only; there is no MCP23017 on the bench, the
// expander side is proven here with a plain struct, not with a chip.
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include "../../src/pin_status_json.h"
#include "../../src/pin_assign_json.h"
#include "../../src/gpio_status_json.h"

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
    if (*v.p == '-' && v.p[1] >= '0' && v.p[1] <= '9') v.p++; // /gpiostatus lvl -1
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

// ---- GET /pinassign (src/pin_assign_json.h) ----
static char g_ab[PIN_ASSIGN_JSON_MAX];

static PinAssignIn assignBase(void)
{
    PinAssignIn in;
    memset(&in, 0, sizeof(in));
    in.chip = PIN_CHIP_ESP32;
    for (int i = 0; i < PIN_FN_COUNT; i++)
        in.fn[i].ok = 1;
    return in;
}

static void test_assign_shape_and_order(void)
{
    PinAssignIn in = assignBase();
    in.fn[PIN_FN_ONEWIRE].avail = true;
    in.fn[PIN_FN_ONEWIRE].def = 4;
    in.fn[PIN_FN_ONEWIRE].eff = 4;
    in.fn[PIN_FN_BUTTON].avail = true;
    in.fn[PIN_FN_BUTTON].restart = true;
    in.fn[PIN_FN_BUTTON].on = true;
    const size_t n = pinAssignJson(in, g_ab, sizeof(g_ab));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_UINT32(strlen(g_ab), n);
    TEST_ASSERT_TRUE(validJson(g_ab));
    TEST_ASSERT_EQUAL_STRING(
        "{\"chip\":\"esp32\",\"fn\":["
        "{\"id\":\"ow\",\"param\":\"onewiregpio\",\"avail\":1,\"pin\":0,\"def\":4,\"eff\":4,\"on\":0,\"restart\":0,\"ok\":\"0x0000000000000001\"},"
        "{\"id\":\"btn\",\"param\":\"buttongpio\",\"avail\":1,\"pin\":0,\"def\":0,\"eff\":0,\"on\":1,\"restart\":1,\"ok\":\"0x0000000000000001\"},"
        "{\"id\":\"an\",\"param\":\"angpio\",\"avail\":0,\"pin\":0,\"def\":0,\"eff\":0,\"on\":0,\"restart\":0,\"ok\":\"0x0000000000000001\"},"
        "{\"id\":\"wind\",\"param\":\"windpin\",\"avail\":0,\"pin\":0,\"def\":0,\"eff\":0,\"on\":0,\"restart\":0,\"ok\":\"0x0000000000000001\"}]}",
        g_ab);
}

static void test_assign_chip_names(void)
{
    PinAssignIn in = assignBase();
    const PinChip c[] = {PIN_CHIP_ESP32, PIN_CHIP_ESP32S3, PIN_CHIP_NRF52840, PIN_CHIP_NONE};
    const char *nm[] = {"\"chip\":\"esp32\"", "\"chip\":\"esp32s3\"", "\"chip\":\"nrf52840\"", "\"chip\":\"none\""};
    for (int i = 0; i < 4; i++)
    {
        in.chip = c[i];
        TEST_ASSERT_TRUE(pinAssignJson(in, g_ab, sizeof(g_ab)) > 0);
        TEST_ASSERT_NOT_NULL(strstr(g_ab, nm[i]));
        TEST_ASSERT_TRUE(validJson(g_ab));
    }
}

static void test_assign_clamps_numbers(void)
{
    PinAssignIn in = assignBase();
    in.fn[0].pin = -5;
    in.fn[0].def = 99999;
    in.fn[0].eff = 255;
    TEST_ASSERT_TRUE(pinAssignJson(in, g_ab, sizeof(g_ab)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(g_ab, "\"pin\":0,\"def\":255,\"eff\":255"));
}

static void test_assign_mask_full_width(void)
{
    PinAssignIn in = assignBase();
    in.fn[0].ok = 0x8000000000000010ULL;
    in.fn[1].ok = 0xFFFFFFFFFFFFFFFFULL;
    TEST_ASSERT_TRUE(pinAssignJson(in, g_ab, sizeof(g_ab)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(g_ab, "\"ok\":\"0x8000000000000010\""));
    TEST_ASSERT_NOT_NULL(strstr(g_ab, "\"ok\":\"0xffffffffffffffff\""));
}

static void test_assign_mask_from_policy(void)
{
    PinContext c;
    c.chip = PIN_CHIP_ESP32;
    c.reserved = (1ULL << 18) | (1ULL << 23);   // radio
    c.soft = 1ULL << 21;                        // I2C
    c.claimed = 1ULL << 13;
    const uint64_t ow = pinAssignMask(c, PIN_FN_ONEWIRE, 4);
    const uint64_t an = pinAssignMask(c, PIN_FN_ANALOG, 36);
    TEST_ASSERT_TRUE(ow & 1ULL);                  // 0 = default / off
    TEST_ASSERT_TRUE(ow & (1ULL << 4));           // the board default
    TEST_ASSERT_TRUE(ow & (1ULL << 25));          // unreviewed: allowed
    TEST_ASSERT_FALSE(ow & (1ULL << 18));         // reserved
    TEST_ASSERT_FALSE(ow & (1ULL << 21));         // soft
    TEST_ASSERT_FALSE(ow & (1ULL << 13));         // claimed
    TEST_ASSERT_FALSE(ow & (1ULL << 1));          // console
    TEST_ASSERT_FALSE(ow & (1ULL << 6));          // flash
    TEST_ASSERT_FALSE(ow & (1ULL << 24));         // hole
    TEST_ASSERT_FALSE(ow & (1ULL << 40));         // past the chip (max 39)
    TEST_ASSERT_FALSE(ow & (1ULL << 36));         // input-only: no output for 1-Wire
    TEST_ASSERT_TRUE(an & (1ULL << 36));          // input-only is fine for analog
    for (int n = 1; n < 64; n++)                  // every bit agrees with pinCanAssign()
        TEST_ASSERT_EQUAL(pinCanAssign(c, PIN_FN_ONEWIRE, n, 4) ? 1 : 0, (int)((ow >> n) & 1));
    PinContext none;                              // no chip: only 0
    TEST_ASSERT_EQUAL_UINT64(1ULL, pinAssignMask(none, PIN_FN_WIND, 0));
}

// The board default is exempt from every class (pinCanAssign): a default that sits on a reserved / soft / claimed pin is
// still assignable, and only that pin gets the exemption.
static void test_assign_mask_default_exemption(void)
{
    PinContext c;
    c.chip = PIN_CHIP_ESP32;
    c.reserved = (1ULL << 18) | (1ULL << 23);
    c.soft = 1ULL << 21;
    c.claimed = 1ULL << 13;
    const uint64_t other = pinAssignMask(c, PIN_FN_ONEWIRE, 4);   // default on a free pin
    TEST_ASSERT_FALSE(other & (1ULL << 18));
    TEST_ASSERT_FALSE(other & (1ULL << 21));
    TEST_ASSERT_FALSE(other & (1ULL << 13));
    const int dflt[] = {18, 21, 13, 1, 6};                        // HARD, SOFT, claimed, console, flash
    for (int i = 0; i < 5; i++)
    {
        const uint64_t m = pinAssignMask(c, PIN_FN_ONEWIRE, dflt[i]);
        TEST_ASSERT_TRUE_MESSAGE(m & (1ULL << dflt[i]), "default on a refused pin is assignable");
        TEST_ASSERT_EQUAL_UINT64(other & ~(1ULL << dflt[i]) & ~(1ULL << 4), m & ~(1ULL << dflt[i]) & ~(1ULL << 4));   // nothing else changes
    }
    TEST_ASSERT_FALSE(pinAssignMask(c, PIN_FN_ONEWIRE, 4) & (1ULL << 18));
}

// The mask follows the console setter: analog takes no 0, 1-Wire on BOARD_RAK4630 no 0-2 (pinBoardSetterRefused()).
static void test_assign_mask_setter_refusals(void)
{
    PinContext c;
    c.chip = PIN_CHIP_NRF52840;
    c.reserved = c.soft = c.claimed = 0;
    TEST_ASSERT_TRUE(pinAssignMask(c, PIN_FN_ANALOG, 5) & 1ULL);              // nothing refused: bit 0 stays
    const uint64_t an = pinAssignMask(c, PIN_FN_ANALOG, 5, 1ULL);
    TEST_ASSERT_FALSE(an & 1ULL);
    TEST_ASSERT_TRUE(an & (1ULL << 5));
    TEST_ASSERT_TRUE(an & (1ULL << 3));
    const uint64_t ow = pinAssignMask(c, PIN_FN_ONEWIRE, 17, 7ULL);
    TEST_ASSERT_EQUAL_UINT64(0ULL, ow & 7ULL);
    TEST_ASSERT_TRUE(ow & (1ULL << 17));                                      // the default
    TEST_ASSERT_TRUE(ow & (1ULL << 3));
    TEST_ASSERT_EQUAL_UINT64(pinAssignMask(c, PIN_FN_ONEWIRE, 17) & ~7ULL, ow);   // only those bits differ
}

static void test_assign_def_eff_minus_one_is_none_zero_is_gpio0(void)
{
    PinAssignIn in;
    memset(&in, 0, sizeof(in));
    in.chip = PIN_CHIP_ESP32;
    in.fn[PIN_FN_ONEWIRE].def = -1;
    in.fn[PIN_FN_ONEWIRE].eff = -1;
    in.fn[PIN_FN_BUTTON].def = 0;   // GPIO 0 button (13 variants)
    in.fn[PIN_FN_BUTTON].eff = 0;
    in.fn[PIN_FN_ANALOG].pin = -5;  // stored values stay clamped to 0
    char buf[PIN_ASSIGN_JSON_MAX];
    TEST_ASSERT_TRUE(pinAssignJson(in, buf, sizeof(buf)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":\"ow\",\"param\":\"onewiregpio\",\"avail\":0,\"pin\":0,\"def\":-1,\"eff\":-1,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":\"btn\",\"param\":\"buttongpio\",\"avail\":0,\"pin\":0,\"def\":0,\"eff\":0,"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"id\":\"an\",\"param\":\"angpio\",\"avail\":0,\"pin\":0,"));
}

static void test_assign_worst_case_fits_max(void)
{
    PinAssignIn in = assignBase();
    const PinChip chips[] = {PIN_CHIP_NRF52840, PIN_CHIP_ESP32S3, PIN_CHIP_ESP32, PIN_CHIP_NONE};
    size_t worst = 0;
    for (int ci = 0; ci < 4; ci++)
    {
        in.chip = chips[ci];
        for (int i = 0; i < PIN_FN_COUNT; i++)
        {
            in.fn[i].avail = in.fn[i].on = in.fn[i].restart = true;
            in.fn[i].pin = in.fn[i].def = in.fn[i].eff = 255;   // clamp ceiling: three digits
            in.fn[i].ok = 0xFFFFFFFFFFFFFFFFULL;
        }
        const size_t n = pinAssignJson(in, g_ab, sizeof(g_ab));
        TEST_ASSERT_TRUE(n > 0);
        TEST_ASSERT_TRUE(validJson(g_ab));
        if (n > worst) worst = n;
    }
    // pinned: the true worst case (nrf52840, everything at its widest); the buffer stays close and under the 1 KB stack rule
    TEST_ASSERT_EQUAL_UINT32(495, worst);
    TEST_ASSERT_TRUE(PIN_ASSIGN_JSON_MAX >= worst + 1);
    TEST_ASSERT_TRUE(PIN_ASSIGN_JSON_MAX <= worst + 64);
    TEST_ASSERT_TRUE(PIN_ASSIGN_JSON_MAX <= 1024);
    char exact[PIN_ASSIGN_JSON_MAX];
    in.chip = PIN_CHIP_NRF52840;
    TEST_ASSERT_EQUAL_UINT32(worst, pinAssignJson(in, exact, worst + 1));
    TEST_ASSERT_EQUAL_UINT32(0, pinAssignJson(in, exact, worst));
    TEST_ASSERT_EQUAL('\0', exact[0]);
}

static void test_assign_buffer_limit(void)
{
    PinAssignIn in = assignBase();
    char tiny[8];
    TEST_ASSERT_EQUAL_UINT32(0, pinAssignJson(in, tiny, sizeof(tiny)));
    TEST_ASSERT_EQUAL('\0', tiny[0]);
    TEST_ASSERT_EQUAL_UINT32(0, pinAssignJson(in, NULL, 100));
    TEST_ASSERT_EQUAL_UINT32(0, pinAssignJson(in, g_ab, 0));
}


// ---- GET /gpiostatus (src/gpio_status_json.h) ----
void test_gpio_status_avail_zero_is_empty_doc(void)
{
    GpioStatusIn in = {false, false, NULL, 0};
    char b[GPIO_STATUS_JSON_MAX];
    TEST_ASSERT_EQUAL(strlen("{\"avail\":0,\"master\":0,\"pins\":[]}"), gpioStatusJson(in, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("{\"avail\":0,\"master\":0,\"pins\":[]}", b);
    TEST_ASSERT_TRUE(validJson(b));
    // pins are ignored when not available
    GpioStatusPin p[1] = {{13, "", 1, 1}};
    GpioStatusIn in2 = {false, true, p, 1};
    gpioStatusJson(in2, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("{\"avail\":0,\"master\":1,\"pins\":[]}", b);
}
void test_gpio_status_master_off_and_on(void)
{
    GpioStatusPin p[2] = {{13, "", 0, -1}, {14, NULL, 0, -1}};
    char b[GPIO_STATUS_JSON_MAX];
    GpioStatusIn off = {true, false, p, 2};
    TEST_ASSERT_TRUE(gpioStatusJson(off, b, sizeof(b)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"avail\":1,\"master\":0,\"pins\":[{\"pin\":13,\"own\":\"\",\"dir\":\"-\",\"lvl\":-1},{\"pin\":14,\"own\":\"\",\"dir\":\"-\",\"lvl\":-1}]}", b);
    TEST_ASSERT_TRUE(validJson(b));
    p[0].dir = 2; p[0].level = 1; p[1].dir = 1; p[1].level = 0;
    GpioStatusIn on = {true, true, p, 2};
    TEST_ASSERT_TRUE(gpioStatusJson(on, b, sizeof(b)) > 0);
    TEST_ASSERT_EQUAL_STRING("{\"avail\":1,\"master\":1,\"pins\":[{\"pin\":13,\"own\":\"\",\"dir\":\"out\",\"lvl\":1},{\"pin\":14,\"own\":\"\",\"dir\":\"in\",\"lvl\":0}]}", b);
    TEST_ASSERT_TRUE(validJson(b));
}
void test_gpio_status_owner_row_and_sanitising(void)
{
    GpioStatusPin p[3] = {{4, "ow", 0, -1}, {2, "led", 0, 7}, {5, "a\"b\\c-d_e123456789", 0, -1}};
    GpioStatusIn in = {true, true, p, 3};
    char b[GPIO_STATUS_JSON_MAX];
    TEST_ASSERT_TRUE(gpioStatusJson(in, b, sizeof(b)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(b, "{\"pin\":4,\"own\":\"ow\",\"dir\":\"-\",\"lvl\":-1}"));
    TEST_ASSERT_NOT_NULL(strstr(b, "{\"pin\":2,\"own\":\"led\",\"dir\":\"-\",\"lvl\":-1}")); // bad level -> -1
    TEST_ASSERT_NOT_NULL(strstr(b, "\"own\":\"abcd_e12\""));                                         // no quote / backslash, 8 chars
    TEST_ASSERT_TRUE(validJson(b));
}
void test_gpio_status_level_unknown_is_minus_one(void)
{
    GpioStatusPin p[1] = {{13, "", 1, -1}};
    GpioStatusIn in = {true, true, p, 1};
    char b[GPIO_STATUS_JSON_MAX];
    TEST_ASSERT_TRUE(gpioStatusJson(in, b, sizeof(b)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(b, "\"dir\":\"in\",\"lvl\":-1"));
}
void test_gpio_status_buffer_too_small_fails(void)
{
    GpioStatusPin p[2] = {{13, "", 2, 1}, {14, "wind", 0, -1}};
    GpioStatusIn in = {true, true, p, 2};
    char b[GPIO_STATUS_JSON_MAX];
    const size_t full = gpioStatusJson(in, b, sizeof(b));
    TEST_ASSERT_TRUE(full > 0);
    char s[GPIO_STATUS_JSON_MAX];
    for (size_t sz = 1; sz <= full; sz++) // one byte short of the NUL-terminated document and below: failure, nothing half written
    {
        memset(s, 'x', sizeof(s));
        TEST_ASSERT_EQUAL_MESSAGE(0, gpioStatusJson(in, s, sz), "must fail");
        TEST_ASSERT_EQUAL(0, s[0]);
    }
    TEST_ASSERT_EQUAL(full, gpioStatusJson(in, s, full + 1)); // exactly enough
    TEST_ASSERT_EQUAL(0, gpioStatusJson(in, NULL, 10));
    TEST_ASSERT_EQUAL(0, gpioStatusJson(in, s, 0));
}
void test_gpio_status_worst_case_fits_max(void)
{
    GpioStatusPin p[GPIO_STATUS_MAX_PINS];
    for (int i = 0; i < GPIO_STATUS_MAX_PINS; i++) { p[i].pin = 255; p[i].own = "wind"; p[i].dir = 2; p[i].level = -1; }
    GpioStatusIn in = {true, true, p, GPIO_STATUS_MAX_PINS};
    char b[GPIO_STATUS_JSON_MAX];
    TEST_ASSERT_TRUE(gpioStatusJson(in, b, sizeof(b)) > 0);
    TEST_ASSERT_TRUE(validJson(b));
}

// GPIO-06: T-Deck Pro pin reservation. src/pin_board.cpp is not compiled in the native envs (it needs the board macros), so
// this reads the two sources: the value of each enable / keyboard define in variants/t_deck_pro/configuration.h, and that
// kHard[] in src/pin_board.cpp lists the macro under the Pro guard.
static std::string gpio06_read(const char *rel)
{
    char cwd[4096];
    TEST_ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    std::string dir(cwd);
    for (int hops = 0; hops < 16; hops++)
    {
        std::ifstream probe(dir + "/platformio.ini");
        if (probe.good()) break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0) TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }
    std::ifstream f((dir + "/" + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), rel);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

static int gpio06_define(const std::string &cfg, const char *name)
{
    const std::string key = std::string("#define ") + name;
    size_t at = 0;
    while ((at = cfg.find(key, at)) != std::string::npos)
    {
        const size_t e = at + key.size();
        if (e < cfg.size() && (cfg[e] == ' ' || cfg[e] == '\t'))
            return atoi(cfg.c_str() + e);
        at = e;
    }
    return -1;
}

static void test_tdeck_pro_enable_and_keyboard_pins_are_reserved(void)
{
    const std::string cfg = gpio06_read("variants/t_deck_pro/configuration.h");
    const std::string src = gpio06_read("src/pin_board.cpp");
    const size_t hardAt = src.find("constexpr int kHard[]");
    const size_t softAt = src.find("constexpr int kSoft[]");
    TEST_ASSERT_TRUE(hardAt != std::string::npos && softAt != std::string::npos && hardAt < softAt);
    const std::string hard = src.substr(hardAt, softAt - hardAt);

    struct { const char *name; int pin; } want[] = {
        {"TDECK_KEYBOARD_INT", 15}, {"BOARD_GPS_EN", 39}, {"BOARD_1V8_EN", 38}, {"BOARD_6609_EN", 41}, {"BOARD_LORA_EN", 46}};
    // GPIO-08: SD CS and touch IRQ follow src/t-deck-pro/utilities.h (BOARD_SD_CS 48, BOARD_TOUCH_INT 12), not the T-Deck copy
    TEST_ASSERT_EQUAL_INT_MESSAGE(48, gpio06_define(cfg, "TDECK_SDCARD_CS"), "TDECK_SDCARD_CS");
    TEST_ASSERT_EQUAL_INT_MESSAGE(12, gpio06_define(cfg, "TDECK_TOUCH_INT"), "TDECK_TOUCH_INT");
    TEST_ASSERT_TRUE_MESSAGE(src.find("BOARD_GPS_EN == 39 && (kHardMask & (1ULL << BOARD_GPS_EN))") != std::string::npos, "GPS EN static_assert");
    // GPIO-09: the T-Deck audio (ES7210), TFT and power-on copies are wrong on the Pro (48 = SD CS, 42 = keyboard LED,
    // 10 = A7682E RXD, e-paper instead of a TFT) and unused there, so the Pro variant must not define them at all.
    const char *gone[] = {"TDECK_POWERON",     "TDECK_TFT_CS",      "TDECK_TFT_DC",     "TDECK_TFT_BACKLIGHT",
                          "TDECK_ES7210_MCLK", "TDECK_ES7210_LRCK", "TDECK_ES7210_SCK", "TDECK_ES7210_DIN"};
    for (const char *g : gone)
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, gpio06_define(cfg, g), g);
    TEST_ASSERT_TRUE_MESSAGE(src.find("GPIO-09: T-Deck copies") != std::string::npos, "GPIO-09 #error guard");
    for (const auto &w : want)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(w.pin, gpio06_define(cfg, w.name), w.name);
        TEST_ASSERT_TRUE_MESSAGE(hard.find(std::string("WX_RES(") + w.name + ")") != std::string::npos, w.name);
        TEST_ASSERT_TRUE_MESSAGE(src.find(std::string("(1ULL << ") + w.name + ")") != std::string::npos, w.name);
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_tdeck_pro_enable_and_keyboard_pins_are_reserved);
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
    RUN_TEST(test_assign_shape_and_order);
    RUN_TEST(test_assign_chip_names);
    RUN_TEST(test_assign_clamps_numbers);
    RUN_TEST(test_assign_mask_full_width);
    RUN_TEST(test_assign_mask_from_policy);
    RUN_TEST(test_assign_mask_default_exemption);
    RUN_TEST(test_assign_mask_setter_refusals);
    RUN_TEST(test_assign_def_eff_minus_one_is_none_zero_is_gpio0);
    RUN_TEST(test_assign_worst_case_fits_max);
    RUN_TEST(test_assign_buffer_limit);
    RUN_TEST(test_gpio_status_avail_zero_is_empty_doc);
    RUN_TEST(test_gpio_status_master_off_and_on);
    RUN_TEST(test_gpio_status_owner_row_and_sanitising);
    RUN_TEST(test_gpio_status_level_unknown_is_minus_one);
    RUN_TEST(test_gpio_status_buffer_too_small_fails);
    RUN_TEST(test_gpio_status_worst_case_fits_max);
    return UNITY_END();
}
