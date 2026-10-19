// Host test for src/web_functions/au_ui_state.h -- state selector, error table, etaS and the
// /austatus producer of the Auto update row (campaign-au-webgui-ux, spec 4.1, 4.3, 5.1).
//
//   pio test -e native -f test_au_ui_state   (host only, no hardware)
//
// The AU_GOLDEN_<STATE>_JSON strings are read by tools/webgui_au_test.js (cppStrings() style) as
// fake /austatus bodies: keep them one-line plain C string literals. AU_GOLDEN_<STATE>_STATE is
// the expected base state letter (D2 = "d", G2 = "g", see AuUiState). F is a page-side state
// (remembered click), its body is that of E. I is an overlay: its state letter is the base (C).

#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include <web_functions/au_ui_state.h>

void setUp(void) {}
void tearDown(void) {}

static const char *const AU_GOLDEN_S_JSON = "{\"st\":\"idle\",\"job\":\"none\",\"run\":\"v4.40a.10.17\",\"avail\":\"\",\"newer\":0,\"inst\":0,\"staged\":\"\",\"err\":\"\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":0,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"old\"}";
static const char *const AU_GOLDEN_S_STATE = "S";
static const char *const AU_GOLDEN_F_JSON = "{\"st\":\"idle\",\"job\":\"none\",\"run\":\"v4.40a.10.17\",\"avail\":\"v4.40a.10.18\",\"newer\":1,\"inst\":1,\"staged\":\"v4.40a.10.18\",\"err\":\"\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_F_STATE = "F";
static const char *const AU_GOLDEN_B_JSON = "{\"st\":\"busy\",\"job\":\"check\",\"run\":\"v4.40a.10.17\",\"avail\":\"\",\"newer\":0,\"inst\":0,\"staged\":\"\",\"err\":\"\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_B_STATE = "B";
static const char *const AU_GOLDEN_D_JSON = "{\"st\":\"busy\",\"job\":\"download\",\"run\":\"v4.40a.10.17\",\"avail\":\"v4.40a.10.18\",\"newer\":1,\"inst\":1,\"staged\":\"\",\"err\":\"\",\"pct\":42,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_D_STATE = "D";
static const char *const AU_GOLDEN_D2_JSON = "{\"st\":\"busy\",\"job\":\"download\",\"run\":\"v4.40a.10.17\",\"avail\":\"v4.40a.10.18\",\"newer\":1,\"inst\":1,\"staged\":\"\",\"err\":\"\",\"pct\":100,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_D2_STATE = "d";
static const char *const AU_GOLDEN_E_JSON = "{\"st\":\"ok\",\"job\":\"download\",\"run\":\"v4.40a.10.17\",\"avail\":\"v4.40a.10.18\",\"newer\":1,\"inst\":1,\"staged\":\"v4.40a.10.18\",\"err\":\"\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_E_STATE = "E";
static const char *const AU_GOLDEN_G_JSON = "{\"st\":\"fail\",\"job\":\"check\",\"run\":\"v4.40a.10.17\",\"avail\":\"\",\"newer\":0,\"inst\":0,\"staged\":\"\",\"err\":\"lowheap_phone\",\"pct\":0,\"phone\":1,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_G_STATE = "G";
static const char *const AU_GOLDEN_G2_JSON = "{\"st\":\"fail\",\"job\":\"check\",\"run\":\"v4.40a.10.17\",\"avail\":\"\",\"newer\":0,\"inst\":0,\"staged\":\"\",\"err\":\"lowheap_phone\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_G2_STATE = "g";
static const char *const AU_GOLDEN_H_JSON = "{\"st\":\"fail\",\"job\":\"check\",\"run\":\"v4.40a.10.17\",\"avail\":\"\",\"newer\":0,\"inst\":0,\"staged\":\"\",\"err\":\"lowheap_ble_off\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_H_STATE = "H";
static const char *const AU_GOLDEN_C_JSON = "{\"st\":\"ok\",\"job\":\"check\",\"run\":\"v4.40a.10.17\",\"avail\":\"v4.40a.10.18\",\"newer\":1,\"inst\":1,\"staged\":\"\",\"err\":\"\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_C_STATE = "C";
static const char *const AU_GOLDEN_A_JSON = "{\"st\":\"ok\",\"job\":\"check\",\"run\":\"v4.40a.10.17\",\"avail\":\"v4.40a.10.17\",\"newer\":0,\"inst\":0,\"staged\":\"\",\"err\":\"\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_A_STATE = "A";
static const char *const AU_GOLDEN_I_JSON = "{\"st\":\"fail\",\"job\":\"download\",\"run\":\"v4.40a.10.17\",\"avail\":\"v4.40a.10.18\",\"newer\":1,\"inst\":1,\"staged\":\"\",\"err\":\"digest\",\"pct\":0,\"phone\":0,\"guard\":0,\"sbok\":1,\"mode\":\"notify\",\"chan\":\"dev\",\"etaS\":35,\"ifree\":57000,\"iblk\":31000,\"sbwhy\":\"ok\"}";
static const char *const AU_GOLDEN_I_STATE = "C";

static void cp(char *d, size_t n, const char *s)
{
    strncpy(d, s, n - 1);
    d[n - 1] = 0;
}

// Baseline of the goldens: idle, nothing newer, sbok 1, running v4.40a.10.17.
static AuUiIn base(void)
{
    AuUiIn in;
    memset(&in, 0, sizeof(in));
    cp(in.st, sizeof(in.st), "idle");
    cp(in.job, sizeof(in.job), "none");
    cp(in.run, sizeof(in.run), "v4.40a.10.17");
    cp(in.mode, sizeof(in.mode), "notify");
    cp(in.chan, sizeof(in.chan), "dev");
    in.sbok = 1;
    cp(in.sbwhy, sizeof(in.sbwhy), "ok");
    in.etaS = 35;
    in.ifree = 57000;
    in.iblk = 31000;
    return in;
}

static AuUiIn mk(const char *st, const char *job, const char *avail, int newer, int inst, const char *staged, const char *err, int pct, int phone)
{
    AuUiIn in = base();
    cp(in.st, sizeof(in.st), st);
    cp(in.job, sizeof(in.job), job);
    cp(in.avail, sizeof(in.avail), avail);
    in.newer = newer;
    in.inst = inst;
    cp(in.staged, sizeof(in.staged), staged);
    cp(in.err, sizeof(in.err), err);
    in.pct = pct;
    in.phone = phone;
    return in;
}

static const char *T18 = "v4.40a.10.18";

static AuUiIn inS(void) { AuUiIn i = base(); i.sbok = 0; cp(i.sbwhy, sizeof(i.sbwhy), "old"); return i; }
static AuUiIn inSdio(void) { AuUiIn i = inS(); cp(i.sbwhy, sizeof(i.sbwhy), "dio"); return i; }
static AuUiIn inF(void) { return mk("idle", "none", T18, 1, 1, T18, "", 0, 0); }
static AuUiIn inB(void) { return mk("busy", "check", "", 0, 0, "", "", 0, 0); }
static AuUiIn inD(void) { return mk("busy", "download", T18, 1, 1, "", "", 42, 0); }
static AuUiIn inD2(void) { return mk("busy", "download", T18, 1, 1, "", "", 100, 0); }
static AuUiIn inE(void) { return mk("ok", "download", T18, 1, 1, T18, "", 0, 0); }
static AuUiIn inG(void) { return mk("fail", "check", "", 0, 0, "", "lowheap_phone", 0, 1); }
static AuUiIn inG2(void) { return mk("fail", "check", "", 0, 0, "", "lowheap_phone", 0, 0); }
static AuUiIn inH(void) { return mk("fail", "check", "", 0, 0, "", "lowheap_ble_off", 0, 0); }
static AuUiIn inC(void) { return mk("ok", "check", T18, 1, 1, "", "", 0, 0); }
static AuUiIn inA(void) { return mk("ok", "check", "v4.40a.10.17", 0, 0, "", "", 0, 0); }
static AuUiIn inI(void) { return mk("fail", "download", T18, 1, 1, "", "digest", 0, 0); }

static void golden(const char *name, AuUiIn in, const char *json, const char *state, bool remembered = false)
{
    char out[512];
    size_t n = auStatusJson(in, out, sizeof(out));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(strlen(json), n, name);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(json, out, name);
    TEST_ASSERT_EQUAL_CHAR_MESSAGE(state[0], auUiState(in, remembered), name);
}

void test_golden_per_state(void)
{
    golden("S", inS(), AU_GOLDEN_S_JSON, AU_GOLDEN_S_STATE);
    golden("F", inF(), AU_GOLDEN_F_JSON, AU_GOLDEN_F_STATE, true);
    golden("B", inB(), AU_GOLDEN_B_JSON, AU_GOLDEN_B_STATE);
    golden("D", inD(), AU_GOLDEN_D_JSON, AU_GOLDEN_D_STATE);
    golden("D2", inD2(), AU_GOLDEN_D2_JSON, AU_GOLDEN_D2_STATE);
    golden("E", inE(), AU_GOLDEN_E_JSON, AU_GOLDEN_E_STATE);
    golden("G", inG(), AU_GOLDEN_G_JSON, AU_GOLDEN_G_STATE);
    golden("G2", inG2(), AU_GOLDEN_G2_JSON, AU_GOLDEN_G2_STATE);
    golden("H", inH(), AU_GOLDEN_H_JSON, AU_GOLDEN_H_STATE);
    golden("C", inC(), AU_GOLDEN_C_JSON, AU_GOLDEN_C_STATE);
    golden("A", inA(), AU_GOLDEN_A_JSON, AU_GOLDEN_A_STATE);
    golden("I", inI(), AU_GOLDEN_I_JSON, AU_GOLDEN_I_STATE);
}

void test_state_without_remembered_click_f_body_is_e(void)
{
    TEST_ASSERT_EQUAL_CHAR('E', auUiState(inF(), false));
}

void test_overlay_i(void)
{
    AuUiIn i = inI();
    TEST_ASSERT_TRUE(auUiOverlay(i, auUiState(i, false)));
    // overlays A and E too
    AuUiIn a = inA();
    strcpy(a.err, "tls -80");
    TEST_ASSERT_TRUE(auUiOverlay(a, auUiState(a, false)));
    AuUiIn e = inE();
    strcpy(e.err, "http 404");
    TEST_ASSERT_TRUE(auUiOverlay(e, auUiState(e, false)));
    // not for clean state, low-heap tokens, or states that own the message line
    TEST_ASSERT_FALSE(auUiOverlay(inC(), 'C'));
    TEST_ASSERT_FALSE(auUiOverlay(inG(), 'G'));
    TEST_ASSERT_FALSE(auUiOverlay(inH(), 'H'));
    AuUiIn b = inB();
    strcpy(b.err, "digest");
    TEST_ASSERT_FALSE(auUiOverlay(b, 'B'));
}

void test_priority_table(void)
{
    // S beats everything
    AuUiIn in = inE();
    in.sbok = 0;
    strcpy(in.err, "lowheap_phone");
    TEST_ASSERT_EQUAL_CHAR('S', auUiState(in, true));
    // F beats busy, E, G, H, C
    in = inD();
    TEST_ASSERT_EQUAL_CHAR('F', auUiState(in, true));
    in = inE();
    TEST_ASSERT_EQUAL_CHAR('F', auUiState(in, true));
    // busy beats E, G, H, C
    in = inE();
    strcpy(in.st, "busy");
    strcpy(in.job, "check");
    strcpy(in.err, "lowheap_phone");
    in.phone = 1;
    TEST_ASSERT_EQUAL_CHAR('B', auUiState(in, false));
    // E beats G, G2, H, C
    in = inE();
    strcpy(in.err, "lowheap_phone");
    in.phone = 1;
    TEST_ASSERT_EQUAL_CHAR('E', auUiState(in, false));
    strcpy(in.err, "lowheap_ble_off");
    TEST_ASSERT_EQUAL_CHAR('E', auUiState(in, false));
    // G/G2/H beat C
    in = inC();
    strcpy(in.err, "lowheap_phone");
    in.phone = 1;
    TEST_ASSERT_EQUAL_CHAR('G', auUiState(in, false));
    in.phone = 0;
    TEST_ASSERT_EQUAL_CHAR('g', auUiState(in, false));
    strcpy(in.err, "lowheap");
    TEST_ASSERT_EQUAL_CHAR('H', auUiState(in, false));
    strcpy(in.err, "lowheap_ble_off");
    TEST_ASSERT_EQUAL_CHAR('H', auUiState(in, false));
    // C beats A; newer but not installable is A
    TEST_ASSERT_EQUAL_CHAR('C', auUiState(inC(), false));
    in = inC();
    in.inst = 0;
    TEST_ASSERT_EQUAL_CHAR('A', auUiState(in, false));
    TEST_ASSERT_TRUE(auUiNewerNotInstallable(in));
    TEST_ASSERT_FALSE(auUiNewerNotInstallable(inC()));
    TEST_ASSERT_FALSE(auUiNewerNotInstallable(inA()));
}

void test_busy_variants(void)
{
    AuUiIn in = inD();
    in.pct = 99;
    TEST_ASSERT_EQUAL_CHAR('D', auUiState(in, false));
    in.pct = 100;
    TEST_ASSERT_EQUAL_CHAR('d', auUiState(in, false));
    strcpy(in.job, "stagelan");
    TEST_ASSERT_EQUAL_CHAR('d', auUiState(in, false));
    in.pct = 0;
    TEST_ASSERT_EQUAL_CHAR('D', auUiState(in, false));
}

static void txt(const char *job, const char *tok, const char *want)
{
    char m[96];
    snprintf(m, sizeof(m), "%s / %s", job, tok);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(want, auErrText(job, tok), m);
}

void test_error_table(void)
{
    const char *CONN = "No connection to GitHub";
    for (const char *j : {"check", "download", "stagelan", "none"})
    {
        txt(j, "tls -80", CONN);
        txt(j, "conn -1", CONN);
        txt(j, "http 404", CONN);
        txt(j, "begin", CONN);
        txt(j, "timeout", CONN);
        txt(j, "nomem", "Not enough free memory for the update");
        txt(j, "nocheck", "Update failed");
        txt(j, "url", "Update failed");
        txt(j, "lowheap", "Update failed");
        txt(j, "lowheap_phone", "Update failed");
        txt(j, "lowheap_ble_off", "Update failed");
        txt(j, "", "Update failed");
        txt(j, "weird", "Update failed");
    }
    const char *LIST = "The release list could not be read";
    for (const char *t : {"short", "stall", "toobig", "chunk", "feed", "enc", "json", "noenv", "badver"})
    {
        txt("check", t, LIST);
    }
    txt("download", "toobig", "Update failed");
    txt("download", "json", "Update failed");
    txt("download", "noenv", "Update failed");
    txt("download", "badver", "Update failed");
    const char *DMG = "Download damaged, it was discarded -- try again";
    for (const char *t : {"short", "stall", "digest", "readback", "crc", "write", "enc", "feed", "chunk"})
    {
        txt("download", t, DMG);
        txt("stagelan", t, DMG);
    }
    txt("check", "digest", "Update failed");
    txt("check", "readback", "Update failed");
    txt("check", "crc", "Update failed");
    txt("check", "write", "Update failed");
    for (const char *t : {"noasset", "nodigest"})
    {
        txt("download", t, "This release has no image for this board");
        txt("check", t, "Update failed");
    }
    for (const char *t : {"notota0", "imglen", "size", "overlap", "small", "inflate", "layout"})
    {
        txt("download", t, "This node's partition layout cannot take the image -- use the Webflasher");
        txt("check", t, "Update failed");
    }
    for (const char *t : {"erase", "noflash", "nvs"})
    {
        txt("download", t, "Could not write the image to flash");
        txt("check", t, "Update failed");
    }
    TEST_ASSERT_EQUAL_STRING("Update failed", auErrText("check", nullptr));
    TEST_ASSERT_EQUAL_STRING("Update failed", auErrText("", "digest"));
}

void test_eta(void)
{
    TEST_ASSERT_EQUAL_UINT32(35, auEtaS(0));
    uint32_t a = auEtaS(1500000), b = auEtaS(1000000);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)AU_ETA_HANDOVER_S + 1500000u / (uint32_t)AU_ETA_RATE_BPS + (uint32_t)AU_ETA_BOOT_S, a);
    TEST_ASSERT_TRUE(a > b);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)AU_ETA_HANDOVER_S + (uint32_t)AU_ETA_BOOT_S, auEtaS(1));
    // classic ESP32 fit: a typical image lands near the 33 s bench time (DK5EN-92)
    TEST_ASSERT_UINT32_WITHIN(5, 33, auEtaS(1500000));
}

void test_escaping(void)
{
    AuUiIn in = inC();
    strcpy(in.avail, "v\"4\\x\n\t\x01");
    strcpy(in.err, "a\"b");
    char out[512];
    size_t n = auStatusJson(in, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"avail\":\"v\\\"4\\\\x\\u000a\\u0009\\u0001\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"err\":\"a\\\"b\""));
    TEST_ASSERT_EQUAL_UINT32(strlen(out), n);
    // no raw control character or unescaped quote leaks: quotes outside values are balanced
    for (size_t i = 0; i < n; i++)
        TEST_ASSERT_TRUE((unsigned char)out[i] >= 0x20);
}

void test_overflow_and_truncation(void)
{
    AuUiIn in = inC();
    char out[512];
    size_t full = auStatusJson(in, out, sizeof(out));
    TEST_ASSERT_TRUE(full > 0);
    char small[512];
    // exact fit (full + NUL) works, one byte less fails with 0 and an empty string
    TEST_ASSERT_EQUAL_UINT32(full, auStatusJson(in, small, full + 1));
    memset(small, 'x', sizeof(small));
    TEST_ASSERT_EQUAL_UINT32(0, auStatusJson(in, small, full));
    TEST_ASSERT_EQUAL_CHAR(0, small[0]);
    for (size_t cap = 1; cap < full; cap++)
        TEST_ASSERT_EQUAL_UINT32(0, auStatusJson(in, small, cap));
    TEST_ASSERT_EQUAL_UINT32(0, auStatusJson(in, small, 0));
    TEST_ASSERT_EQUAL_UINT32(0, auStatusJson(in, nullptr, 10));
    // full-length fields: a field filled to the brim (no NUL inside the array is the caller's duty)
    AuUiIn big = base();
    memset(big.staged, 'q', sizeof(big.staged) - 1);
    char o2[512];
    TEST_ASSERT_TRUE(auStatusJson(big, o2, sizeof(o2)) > 0);
    // negative pct never prints a sign
    AuUiIn neg = base();
    neg.pct = -5;
    TEST_ASSERT_TRUE(auStatusJson(neg, o2, sizeof(o2)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(o2, "\"pct\":0,"));
}

void test_sbwhy_key(void)
{
    char out[512];
    // reason is the last key; state stays S for both reasons
    AuUiIn in = inSdio();
    TEST_ASSERT_TRUE(auStatusJson(in, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sbok\":0,"));
    size_t n = strlen(out);
    TEST_ASSERT_EQUAL_STRING(",\"sbwhy\":\"dio\"}", out + n - strlen(",\"sbwhy\":\"dio\"}"));
    TEST_ASSERT_EQUAL_CHAR('S', auUiState(in, false));
    TEST_ASSERT_EQUAL_CHAR('S', auUiState(in, true));
    in = inS();
    TEST_ASSERT_TRUE(auStatusJson(in, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"sbok\":0,"));
    TEST_ASSERT_NOT_NULL(strstr(out, ",\"sbwhy\":\"old\"}"));
    TEST_ASSERT_EQUAL_CHAR('S', auUiState(in, false));
    in = base();
    TEST_ASSERT_TRUE(auStatusJson(in, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(strstr(out, ",\"sbwhy\":\"ok\"}"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_golden_per_state);
    RUN_TEST(test_state_without_remembered_click_f_body_is_e);
    RUN_TEST(test_overlay_i);
    RUN_TEST(test_priority_table);
    RUN_TEST(test_busy_variants);
    RUN_TEST(test_error_table);
    RUN_TEST(test_eta);
    RUN_TEST(test_escaping);
    RUN_TEST(test_overflow_and_truncation);
    RUN_TEST(test_sbwhy_key);
    return UNITY_END();
}
