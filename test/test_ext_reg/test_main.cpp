// EXTUDP register query: name table, secret filter, framing, limiter (src/ext_reg.h)
//   piolock.sh pio test -e native_ext_reg
#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "../../src/ext_reg.h"

void setUp() {}
void tearDown() {}

static char out[EXT_REG_REPLY_MAX];

static size_t filt(const char *s, bool withType = false)
{
    memset(out, 'X', sizeof(out));
    return extRegFilter(s, strlen(s), out, sizeof(out), withType);
}

// ---- realistic frames (shape from sendBleJsonRegister callers) ----
static const char *FRAME_I =
    "{\"TYP\":\"I\",\"FWVER\":\"4.40a.10.12\",\"CALL\":\"DK5EN-99\",\"ID\":3456789,\"HWID\":43,\"MAXV\":4.15,"
    "\"BLE\":\"short\",\"BATP\":87,\"BATV\":3.98,\"GCB0\":0,\"GCB1\":0,\"GCB2\":0,\"GCB3\":0,\"GCB4\":0,\"GCB5\":0,"
    "\"CTRY\":\"DE\",\"BOOST\":true,\"BPIN\":123456}";
static const char *FRAME_SN1 =
    "{\"TYP\":\"SN1\",\"VIA\":true,\"VIACALL\":\"OE1KFR-12\",\"WSPWD\":\"geheim\",\"ASYM\":false,"
    "\"TZ\":\"CET-1CEST,M3.5.0,M10.5.0/3\",\"MHOP\":5,\"MTU\":1500,\"VOLT\":false,\"NCON\":false}";

static void test_lookup_names(void)
{
    for (int i = 0; i < EXT_REG_COUNT; i++)
        TEST_ASSERT_EQUAL_INT(i, extRegLookup(EXT_REG_TABLE[i].name));
    TEST_ASSERT_EQUAL_INT(EXT_REG_ALL, extRegLookup("all"));
    TEST_ASSERT_EQUAL_INT(EXT_REG_ALL, extRegLookup("ALL"));
    TEST_ASSERT_EQUAL_INT(11, extRegLookup("sn"));
    TEST_ASSERT_EQUAL_INT(12, extRegLookup("sn1"));
}

static void test_lookup_unknown(void)
{
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup(nullptr));
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup(""));
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup("S"));     // no prefix match
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup("SN2"));
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup("SN "));
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup("CONFFINX"));
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup("--info"));
    TEST_ASSERT_EQUAL_INT(EXT_REG_UNKNOWN, extRegLookup("AAAAAAAAAAAAAAAAAAAAAAAA"));
}

static void test_table_commands_are_console_commands(void)
{
    for (int i = 0; i < EXT_REG_COUNT; i++)
    {
        TEST_ASSERT_EQUAL_INT(0, memcmp(EXT_REG_TABLE[i].cmd, "--", 2));
        TEST_ASSERT_TRUE(strlen(EXT_REG_TABLE[i].name) <= EXT_REG_NAME_MAX);
    }
}

static void test_frame_typ(void)
{
    char t[EXT_REG_NAME_MAX + 1];
    TEST_ASSERT_TRUE(extRegFrameTyp(FRAME_SN1, strlen(FRAME_SN1), t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("SN1", t);
    const char *c = "{\"TYP\":\"CONFFIN\"}";
    TEST_ASSERT_TRUE(extRegFrameTyp(c, strlen(c), t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("CONFFIN", t);
    const char *bad = "{\"X\":1}";
    TEST_ASSERT_FALSE(extRegFrameTyp(bad, strlen(bad), t, sizeof(t)));
    const char *cut = "{\"TYP\":\"CONF";
    TEST_ASSERT_FALSE(extRegFrameTyp(cut, strlen(cut), t, sizeof(t)));
}

static void test_key_is_secret(void)
{
    const char *sec[] = {"BPIN", "bpin", "WSPWD", "PWD", "PW", "pw", "PASSWORD", "passphrase", "APIKEY", "KEY",
                         "SECRET", "token", "AUTHTOKEN", "NEWPW2", "WIFIPASS", "MyPwd",
                         "AUTH", "auth", "KISSAUTH", "HMAC", "hmac", "HMACKEY", "CRED", "CREDS", "APICRED",
                         "CODE", "PINCODE", "pincode", "UNLOCKCODE"};
    for (size_t i = 0; i < sizeof(sec) / sizeof(sec[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(extRegKeyIsSecret(sec[i]), sec[i]);
    const char *pub[] = {"TYP", "CALL", "SSID", "TXPOW", "VPOW", "USERPIN", "OWPIN", "WINDP", "VIACALL", "BATV", "EUDPIP", "",
                         "CODEC", "CODES", "SYMCD", "ENCODED", "CODEX"};
    for (size_t i = 0; i < sizeof(pub) / sizeof(pub[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(extRegKeyIsSecret(pub[i]), pub[i]);
}

static void test_i_bpin_removed_at_end(void)
{
    size_t n = filt(FRAME_I);
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NULL(strstr(out, "BPIN"));
    TEST_ASSERT_NULL(strstr(out, "123456"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"BOOST\":true}"));
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
}

static void test_sn1_wspwd_removed_in_middle(void)
{
    filt(FRAME_SN1);
    TEST_ASSERT_NULL(strstr(out, "WSPWD"));
    TEST_ASSERT_NULL(strstr(out, "geheim"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"VIACALL\":\"OE1KFR-12\",\"ASYM\":false,\"TZ\":\"CET-1CEST,M3.5.0,M10.5.0/3\""));
}

static void test_secret_at_start(void)
{
    size_t n = filt("{\"WSPWD\":\"x\",\"TYP\":\"SN1\",\"A\":1}");
    TEST_ASSERT_EQUAL_STRING("{\"TYP\":\"SN1\",\"A\":1}", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
}

static void test_secret_only_member(void)
{
    filt("{\"BPIN\":1}");
    TEST_ASSERT_EQUAL_STRING("{}", out);
}

static void test_two_secrets_adjacent(void)
{
    filt("{\"TYP\":\"X\",\"BPIN\":1,\"WSPWD\":\"a\",\"B\":2}");
    TEST_ASSERT_EQUAL_STRING("{\"TYP\":\"X\",\"B\":2}", out);
}

static void test_values_with_quotes_and_braces(void)
{
    // secret value holding quotes, braces, a comma and a fake key
    filt("{\"TYP\":\"SN1\",\"WSPWD\":\"a\\\"},\\\"B\\\":{\\\\\",\"VIACALL\":\"x}{\\\"y\"}");
    TEST_ASSERT_EQUAL_STRING("{\"TYP\":\"SN1\",\"VIACALL\":\"x}{\\\"y\"}", out);
    // public value that merely looks like a secret key
    filt("{\"TYP\":\"X\",\"ATXT\":\"\\\"BPIN\\\":1,\"}");
    TEST_ASSERT_EQUAL_STRING("{\"TYP\":\"X\",\"ATXT\":\"\\\"BPIN\\\":1,\"}", out);
}

static void test_nested_secret_keys(void)
{
    filt("{\"TYP\":\"X\",\"CFG\":{\"a\":1,\"PwD\":\"s\",\"deep\":{\"token\":{\"k\":[1,2]},\"ok\":true}},\"L\":[{\"KEY\":1,\"z\":2}]}");
    TEST_ASSERT_EQUAL_STRING("{\"TYP\":\"X\",\"CFG\":{\"a\":1,\"deep\":{\"ok\":true}},\"L\":[{\"z\":2}]}", out);
    // secret member whose value is an object holding more secrets
    filt("{\"SECRET\":{\"BPIN\":1},\"A\":2}");
    TEST_ASSERT_EQUAL_STRING("{\"A\":2}", out);
}

static void test_escaped_key_cannot_hide(void)
{
    // "\u0042PIN" decodes to BPIN, "WS\u0050WD" to WSPWD, "W\u0053PWD" too
    filt("{\"\\u0042PIN\":1,\"WS\\u0050WD\":\"a\",\"A\":2}");
    TEST_ASSERT_EQUAL_STRING("{\"A\":2}", out);
    filt("{\"PA\\u0053S\":1,\"A\":2}");
    TEST_ASSERT_EQUAL_STRING("{\"A\":2}", out);
}

static void test_overlong_key_removed(void)
{
    filt("{\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAPWD\":1,\"A\":2}");
    TEST_ASSERT_EQUAL_STRING("{\"A\":2}", out);
}

static void test_whitespace_input(void)
{
    filt(" { \"TYP\" : \"X\" , \"WSPWD\" : \"a b\" , \"B\" : [ 1 , 2 ] } ");
    TEST_ASSERT_EQUAL_STRING("{\"TYP\":\"X\",\"B\":[1,2]}", out);
}

static void test_with_type_prefix(void)
{
    filt(FRAME_SN1, true);
    TEST_ASSERT_EQUAL_INT(0, strncmp(out, "{\"type\":\"reg\",\"TYP\":\"SN1\",", 26));
    TEST_ASSERT_NULL(strstr(out, "WSPWD"));
    filt("{\"BPIN\":1}", true);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"reg\"}", out);
}

static void test_fail_closed(void)
{
    TEST_ASSERT_EQUAL_size_t(0, filt(""));
    TEST_ASSERT_EQUAL_size_t(0, filt("[1]"));
    TEST_ASSERT_EQUAL_size_t(0, filt("{\"A\":1"));            // truncated
    TEST_ASSERT_EQUAL_size_t(0, filt("{\"A\":\"x"));          // cut in a string
    TEST_ASSERT_EQUAL_size_t(0, filt("{\"A\":1}x"));          // trailing garbage
    TEST_ASSERT_EQUAL_size_t(0, filt("{\"A\" 1}"));
    TEST_ASSERT_EQUAL_size_t(0, filt("{\"A\":{\"B\":{\"C\":{\"D\":{\"E\":{\"F\":{\"G\":{\"H\":{\"I\":{\"J\":1}}}}}}}}}}")); // too deep
    TEST_ASSERT_EQUAL('\0', out[0]);                           // nothing left in out
    TEST_ASSERT_EQUAL_size_t(0, extRegFilter(nullptr, 3, out, sizeof(out), false));
    // output buffer too small
    char small[8];
    TEST_ASSERT_EQUAL_size_t(0, extRegFilter(FRAME_I, strlen(FRAME_I), small, sizeof(small), false));
}

static void test_max_payload_fits_reply(void)
{
    // a 244-byte register plus the type member must fit EXT_REG_REPLY_MAX
    char big[EXT_REG_PAYLOAD_MAX + 1];
    memset(big, 0, sizeof(big));
    int n = snprintf(big, sizeof(big), "{\"TYP\":\"SN1\",\"VIACALL\":\"");
    while (n < EXT_REG_PAYLOAD_MAX - 2)
        big[n++] = 'a';
    big[n++] = '"';
    big[n++] = '}';
    TEST_ASSERT_EQUAL_INT(EXT_REG_PAYLOAD_MAX, n);
    size_t r = filt(big, true);
    TEST_ASSERT_EQUAL_size_t((size_t)n + 13, r);
    TEST_ASSERT_TRUE(r < sizeof(out));
}

// ---- reading the firmware source (as test_command_setters does) ----
static bool file_exists(const std::string &p)
{
    std::ifstream f(p.c_str());
    return f.good();
}

// __FILE__ is relative under PlatformIO's native runner: walk up from the working directory until the
// test file and platformio.ini sit side by side.
static std::string read_repo_file(const char *rel)
{
    const std::string self = "test/test_ext_reg/test_main.cpp";
    char cwd_buf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwd_buf, sizeof(cwd_buf)), "getcwd() failed");
    std::string dir(cwd_buf);
    for (int hops = 0; hops < 16; hops++)
    {
        if (file_exists(dir + "/" + self) && file_exists(dir + "/platformio.ini"))
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }
    const std::string path = dir + "/" + rel;
    std::ifstream f(path.c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), ("could not open " + path).c_str());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Keys of the register frames: every  <var>["KEY"] = ...  assignment of src/command_functions.cpp (the
// JsonDocument builders; no other code in that file assigns a string-literal index). Scanned by hand, no regex.
static std::set<std::string> emitted_keys_from_source(void)
{
    std::set<std::string> keys;
    std::istringstream in(read_repo_file("src/command_functions.cpp"));
    std::string line;
    while (std::getline(in, line))
    {
        size_t pos = 0;
        while ((pos = line.find("[\"", pos)) != std::string::npos)
        {
            const size_t end = line.find("\"]", pos + 2);
            if (end == std::string::npos)
                break;
            const std::string key = line.substr(pos + 2, end - pos - 2);
            size_t k = end + 2;
            while (k < line.size() && (line[k] == ' ' || line[k] == '\t'))
                k++;
            const bool assign = k < line.size() && line[k] == '=' && (k + 1 >= line.size() || line[k + 1] != '=');
            const bool ident_before = pos > 0 && (isalnum((unsigned char)line[pos - 1]) || line[pos - 1] == '_');
            bool plain = !key.empty();
            for (size_t c = 0; c < key.size(); c++)
                if (!isalnum((unsigned char)key[c]) && key[c] != '_')
                    plain = false;
            if (assign && ident_before && plain)
                keys.insert(key);
            pos = end + 2;
        }
    }
    return keys;
}

// Every key the register builders emit (derived from command_functions.cpp, not hand-kept): it must either
// be on the reviewed public list (and pass the filter) or on the secret list (and be removed). A key that is
// new in the source is on neither list and FAILS here until someone has looked at it: the filter is a
// denylist, so an unreviewed key would go to the EXTUDP peer.
static void test_all_emitted_keys_audit(void)
{
    // reviewed 2026-11: public, goes out
    const char *pub[] = {
        "TYP", "PARM", "UNIT", "FORMAT", "EQNS", "VALES", "PTIME",
        "TEMP", "TOFFI", "TOUT", "TOFFO", "HUM", "PRES", "QNH", "ALT", "GAS", "CO2", "VBUS", "VSHUNT", "VAMP", "VPOW",
        "MCP23017", "AxOUT", "AxVAL", "BxOUT", "BxVAL",
        "FWVER", "CALL", "ID", "HWID", "MAXV", "BLE", "BATP", "BATV", "GCB0", "GCB1", "GCB2", "GCB3", "GCB4", "GCB5", "CTRY", "BOOST",
        "BDATE", "CHG", "CHGMA",
        "LAT", "LON", "SAT", "SFIX", "HDOP", "RATE", "NEXT", "DIST", "DIRn", "DIRo", "DATE",
        "BME", "BMP", "BMP3", "BMP3F", "AHT", "AHTF", "BMXF", "680", "680F", "811", "811F", "SS", "LPS33", "OW", "OWPIN", "OWF", "USERPIN",
        "INA226", "SHUNT", "IMAX", "SAMP", "SHT", "SHTF", "226", "226F", "UV", "UVF", "WIND", "WINDP",
        "SSID", "IP", "GW", "AP", "DNS", "SUB", "OWNIP", "OWNGW", "OWNMS", "OWNDNS", "OWNNTP", "EUDP", "EUDPIP", "TXPOW",
        "WS", "DISP", "BTN", "MSH", "GPS", "TRACK", "UTCOF", "TXP", "MQRG", "MSF", "MCR", "MBW", "GWNPOS", "NOALL", "NOPMOTHER", "BLED", "CHGLED", "GWS",
        "VIA", "VIACALL", "ASYM", "TZ", "MHOP", "MTU", "VOLT", "NCON", "KISS", "LEDNF",
        "APN", "AFC", "AK", "AFL", "ACK", "ADC", "ADCRAW", "ADCE1", "ADCE2", "ADCSL", "ADCOF", "ADCAT",
        "ATXT", "SYMID", "SYMCD", "NAME"};
    // secrets: must be removed by the filter
    const char *sec[] = {"BPIN", "WSPWD", "PW"};

    std::set<std::string> pubSet(pub, pub + sizeof(pub) / sizeof(pub[0]));
    std::set<std::string> secSet(sec, sec + sizeof(sec) / sizeof(sec[0]));
    const std::set<std::string> keys = emitted_keys_from_source();
    TEST_ASSERT_TRUE_MESSAGE(keys.size() > 100, "source scan found too few keys: the scan is broken");

    for (std::set<std::string>::const_iterator it = keys.begin(); it != keys.end(); ++it)
    {
        const bool isPub = pubSet.count(*it) != 0, isSec = secSet.count(*it) != 0;
        const std::string msg = "key not reviewed (add to pub[] or sec[] after checking it is not a secret): " + *it;
        TEST_ASSERT_TRUE_MESSAGE(isPub || isSec, msg.c_str());
        if (isPub)
            TEST_ASSERT_FALSE_MESSAGE(extRegKeyIsSecret(it->c_str()), it->c_str());
        if (isSec)
            TEST_ASSERT_TRUE_MESSAGE(extRegKeyIsSecret(it->c_str()), it->c_str());
    }
    // the reviewed lists themselves: the secrets are still emitted (else the entry is stale) and removed
    for (size_t i = 0; i < sizeof(sec) / sizeof(sec[0]); i++)
    {
        TEST_ASSERT_TRUE_MESSAGE(keys.count(sec[i]) != 0, sec[i]);
        TEST_ASSERT_TRUE_MESSAGE(extRegKeyIsSecret(sec[i]), sec[i]);
    }
    // a hand-picked public list entry that the new token rules must not catch
    for (size_t i = 0; i < sizeof(pub) / sizeof(pub[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(extRegKeyIsSecret(pub[i]), pub[i]);
}

// ---- state of a query / "all" run ----
static void test_run_all_steps_and_done(void)
{
    ExtRegRun r;
    extRegRunReset(r);
    TEST_ASSERT_FALSE(extRegRunBusy(r));
    int idx = -1;
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_NONE, extRegAllStep(r, 1000, &idx));   // no run, nothing to do
    extRegRunStartAll(r, 1000);
    TEST_ASSERT_TRUE(extRegRunBusy(r));
    uint32_t now = 1000;
    for (int i = 0; i < EXT_REG_COUNT; i++)
    {
        TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_RUN, extRegAllStep(r, now, &idx));
        TEST_ASSERT_EQUAL_INT(i, idx);
        TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_NONE, extRegAllStep(r, now + EXT_REG_ALL_STEP_MS - 1, &idx));   // 100 ms gap
        now += EXT_REG_ALL_STEP_MS;
    }
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_DONE, extRegAllStep(r, now, &idx));
    TEST_ASSERT_FALSE(extRegRunBusy(r));
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_NONE, extRegAllStep(r, now + 1000, &idx));
}

// "all" at index 5, then EXTUDP off (the tick stops being called), a new peer, EXTUDP on: the rest of the old run
// must not go on towards the new peer. resetExternUDP() calls extRegRunReset() for that.
static void test_reset_drops_a_paused_all_run(void)
{
    ExtRegRun r;
    extRegRunReset(r);
    extRegRunStartAll(r, 0);
    int idx = -1;
    uint32_t now = 0;
    for (int i = 0; i < 5; i++, now += EXT_REG_ALL_STEP_MS)
        TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_RUN, extRegAllStep(r, now, &idx));
    TEST_ASSERT_EQUAL_INT(5, r.allIdx);
    r.pending = true;   // a queued query too
    extRegRunReset(r);
    TEST_ASSERT_FALSE(extRegRunBusy(r));
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_NONE, extRegAllStep(r, now + 60000, &idx));   // nothing is asked of the new peer
    // and a fresh run starts clean at register 0
    extRegRunStartAll(r, now + 60000);
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_RUN, extRegAllStep(r, now + 60000, &idx));
    TEST_ASSERT_EQUAL_INT(0, idx);
}

// Why the reset also matters for time: a run paused for more than 2^31 ms (24.8 days) is never due again,
// the signed compare reads the stored deadline as lying in the future. Without the reset the node would
// refuse every new query ("previous query still running") until reboot.
static void test_paused_run_stalls_after_24_8_days_unless_reset(void)
{
    ExtRegRun r;
    extRegRunReset(r);
    extRegRunStartAll(r, 0);
    int idx = -1;
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_RUN, extRegAllStep(r, 0, &idx));
    const uint32_t later = 0x80000000u + 1000u;   // 24.8 days on
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_NONE, extRegAllStep(r, later, &idx));   // stalled
    TEST_ASSERT_TRUE(extRegRunBusy(r));                                        // and busy for ever
    extRegRunReset(r);
    TEST_ASSERT_FALSE(extRegRunBusy(r));
    extRegRunStartAll(r, later);
    TEST_ASSERT_EQUAL_INT(EXT_REG_STEP_RUN, extRegAllStep(r, later, &idx));
}

// The calls that cannot run on the host (extudp_functions.cpp and command_functions.cpp are not compiled by any native
// env) are pinned in the source text: the order of statements is the fix.
static std::string function_body(const std::string &src, const char *head)
{
    const size_t a = src.find(head);
    TEST_ASSERT_TRUE_MESSAGE(a != std::string::npos, head);
    const size_t b = src.find("\n}\n", a);
    TEST_ASSERT_TRUE_MESSAGE(b != std::string::npos, head);
    return src.substr(a, b - a);
}

static void test_wiring_reset_extudp_clears_the_register_state(void)
{
    const std::string src = read_repo_file("src/extudp_functions.cpp");
    const std::string body = function_body(src, "void resetExternUDP()\n{");
    const size_t reg = body.find("extRegReset();");
    const size_t ap = body.find("if(bWIFIAP)");
    TEST_ASSERT_TRUE_MESSAGE(reg != std::string::npos, "resetExternUDP() does not call extRegReset()");
    TEST_ASSERT_TRUE_MESSAGE(ap != std::string::npos && reg < ap, "extRegReset() must come before the bWIFIAP return");
    // the stub of the EXT_NO_SOCKET build and the real one both exist
    TEST_ASSERT_TRUE(src.find("void extRegReset() {}") != std::string::npos);
    TEST_ASSERT_TRUE(src.find("extRegRunReset(regRun_st);") != std::string::npos);
}

// Reg-Out on the serial console only with bLORADEBUG (not on every register of an "all" run)
static void test_wiring_reg_out_log_is_debug_only(void)
{
    const std::string src = read_repo_file("src/extudp_functions.cpp");
    size_t pos = 0;
    int n = 0;
    while ((pos = src.find("[EXT] Reg-Out:", pos)) != std::string::npos)
    {
        const std::string before = src.substr(pos > 60 ? pos - 60 : 0, pos > 60 ? 60 : pos);
        TEST_ASSERT_TRUE_MESSAGE(before.find("if(bLORADEBUG)") != std::string::npos, "Reg-Out printed without bLORADEBUG");
        n++;
        pos += 5;
    }
    TEST_ASSERT_EQUAL_INT(2, n);
}

static void test_error_and_done_json(void)
{
    char b[64];
    size_t n = extRegErrorJson(b, sizeof(b), "unknown");
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"reg\",\"err\":\"unknown\"}", b);
    TEST_ASSERT_EQUAL_size_t(strlen(b), n);
    n = extRegErrorJson(b, sizeof(b), "unavailable");
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"reg\",\"err\":\"unavailable\"}", b);
    TEST_ASSERT_EQUAL_size_t(0, extRegErrorJson(b, 10, "unknown"));
    n = extRegDoneJson(b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"reg\",\"q\":\"all\",\"done\":true}", b);
    TEST_ASSERT_EQUAL_size_t(strlen(b), n);
}

static void test_limiter(void)
{
    ExtRegLimiter l = {0, false};
    TEST_ASSERT_TRUE(extRegAllow(l, 0));        // first one, even at t=0
    TEST_ASSERT_FALSE(extRegAllow(l, 999));
    TEST_ASSERT_FALSE(extRegAllow(l, 500));
    TEST_ASSERT_TRUE(extRegAllow(l, 1000));
    TEST_ASSERT_FALSE(extRegAllow(l, 1999));    // a dropped one does not move the window
    TEST_ASSERT_TRUE(extRegAllow(l, 2000));
    // millis() wrap
    ExtRegLimiter w = {0xFFFFFF00u, true};
    TEST_ASSERT_FALSE(extRegAllow(w, 0x00000010u));  // 272 ms later
    TEST_ASSERT_TRUE(extRegAllow(w, 0x00000400u));   // 1.3 s later
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_lookup_names);
    RUN_TEST(test_lookup_unknown);
    RUN_TEST(test_table_commands_are_console_commands);
    RUN_TEST(test_frame_typ);
    RUN_TEST(test_key_is_secret);
    RUN_TEST(test_i_bpin_removed_at_end);
    RUN_TEST(test_sn1_wspwd_removed_in_middle);
    RUN_TEST(test_secret_at_start);
    RUN_TEST(test_secret_only_member);
    RUN_TEST(test_two_secrets_adjacent);
    RUN_TEST(test_values_with_quotes_and_braces);
    RUN_TEST(test_nested_secret_keys);
    RUN_TEST(test_escaped_key_cannot_hide);
    RUN_TEST(test_overlong_key_removed);
    RUN_TEST(test_whitespace_input);
    RUN_TEST(test_with_type_prefix);
    RUN_TEST(test_fail_closed);
    RUN_TEST(test_max_payload_fits_reply);
    RUN_TEST(test_all_emitted_keys_audit);
    RUN_TEST(test_run_all_steps_and_done);
    RUN_TEST(test_reset_drops_a_paused_all_run);
    RUN_TEST(test_paused_run_stalls_after_24_8_days_unless_reset);
    RUN_TEST(test_wiring_reset_extudp_clears_the_register_state);
    RUN_TEST(test_wiring_reg_out_log_is_debug_only);
    RUN_TEST(test_error_and_done_json);
    RUN_TEST(test_limiter);
    return UNITY_END();
}
