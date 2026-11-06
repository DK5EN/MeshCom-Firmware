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

// ---- EXT-PEER-01 and the wave's other cross-file wiring (not compiled on the host, pinned in the source text) ----

static size_t find_after(const std::string &s, const char *needle, size_t from, const char *msg)
{
    const size_t p = s.find(needle, from);
    TEST_ASSERT_TRUE_MESSAGE(p != std::string::npos, msg);
    return p;
}

// getExternUDP() is the only task that touches the socket: it consumes the flag on every pass, before any early return,
// and acts on the decision. Callers gate on bEXTUDP, so a request made while EXTUDP is off waits for the first pass after
// it is switched on, which is harmless (extudp_refresh.h).
static void test_wiring_get_extern_udp_consumes_the_peer_refresh_flag(void)
{
    const std::string src = read_repo_file("src/extudp_functions.cpp");
    const size_t flag = find_after(src, "static std::atomic<bool> s_peerRefresh", 0, "s_peerRefresh flag is gone");
    const std::string tail = src.substr(flag);
    const std::string body = function_body(tail, "void getExternUDP()\n{");
    const size_t dec = find_after(body, "extudp_refresh_decide(s_peerRefresh.exchange(false)", 0, "the flag is not consumed in the decision call");
    const size_t ex = dec;
    const size_t firstReturn = find_after(body, "return;", 0, "getExternUDP() has no return");
    TEST_ASSERT_TRUE_MESSAGE(ex < firstReturn, "the flag must be consumed before the first early return");
    // RESET -> resetExternUDP(), START -> startExternUDP(), each right under its own test
    const size_t rst = find_after(body, "refresh == EXTUDP_REFRESH_RESET", 0, "no RESET branch");
    const size_t rstCall = find_after(body, "resetExternUDP();", rst, "RESET does not call resetExternUDP()");
    TEST_ASSERT_TRUE_MESSAGE(rstCall - rst < 80, "resetExternUDP() is not under the RESET test");
    const size_t st = find_after(body, "refresh == EXTUDP_REFRESH_START", 0, "no START branch");
    const size_t stCall = find_after(body, "startExternUDP();", st, "START does not call startExternUDP()");
    TEST_ASSERT_TRUE_MESSAGE(stCall - st < 80, "startExternUDP() is not under the START test");
    // the only place the platform allows opening: nRF52 with Ethernet up (never on an uninitialised W5100S, N-20)
    const size_t gate = find_after(body, "const bool mayOpenHere = neth.hasIPaddress;", 0, "the nRF52 open gate (neth.hasIPaddress) is missing");
    find_after(body, "bEXTUDP, hasExternIPaddress, mayOpenHere);", dec, "the decision does not use the Ethernet gate");
    TEST_ASSERT_TRUE_MESSAGE(gate < dec, "the Ethernet gate must be evaluated before the decision");
}

// Both hooks sit inside the extudpip branch: one before the refusal return (the field is emptied there), one
// after save_settings() on the accepted path.
static void test_wiring_extudpip_requests_a_peer_refresh_on_both_paths(void)
{
    const std::string src = read_repo_file("src/command_functions.cpp");
    const size_t a = find_after(src, "commandCheck(msg_text+2, (char*)\"extudpip\") == 0", 0, "extudpip branch not found");
    const size_t b = find_after(src, "\n    else\n    if(commandCheck(", a, "end of the extudpip branch not found");
    const std::string br = src.substr(a, b - a);
    const char *hook = "extudpRequestPeerRefresh();";
    size_t n = 0, pos = 0;
    while ((pos = br.find(hook, pos)) != std::string::npos) { n++; pos += 5; }
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)n, "expected exactly two refresh hooks in the extudpip branch");
    // and none anywhere else in the file
    size_t all = 0; pos = 0;
    while ((pos = src.find(hook, pos)) != std::string::npos) { all++; pos += 5; }
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, (int)all, "a refresh hook outside the extudpip branch");
    const size_t refuse = find_after(br, "extudp_target_reason(tv)", 0, "refusal path not found");
    const size_t refuseRet = find_after(br, "return;", refuse, "refusal path has no return");
    const size_t h1 = find_after(br, hook, refuse, "no hook on the refusal path");
    TEST_ASSERT_TRUE_MESSAGE(h1 < refuseRet, "the refusal hook must come before the refusal return");
    const size_t save = find_after(br, "save_settings();", refuseRet, "accepted path has no save_settings()");
    const size_t h2 = find_after(br, hook, save, "no hook after save_settings()");
    TEST_ASSERT_TRUE_MESSAGE(h2 > save && h2 > h1, "the second hook must follow save_settings()");
}

// startExternUDP(): an empty node_extern (after "--extudpip none" or a refusal) must not reach DNS, begin() or the
// latch. Same threshold as the send path and resetExternUDP().
static void test_wiring_start_extern_udp_has_the_empty_field_gate(void)
{
    const std::string src = read_repo_file("src/extudp_functions.cpp");
    const std::string body = function_body(src, "void startExternUDP()\n{");
    const size_t latch = find_after(body, "if(hasExternIPaddress)", 0, "startExternUDP() lost its latched check");
    const size_t gate = find_after(body, "if((int)strlen(meshcom_settings.node_extern) <= 7)", latch, "no empty-field gate after the latch check");
    const size_t ret = find_after(body, "return;", gate, "empty-field gate does not return");
    TEST_ASSERT_TRUE_MESSAGE(ret - gate < 80, "empty-field gate does not return directly");
    TEST_ASSERT_TRUE_MESSAGE(gate < find_after(body, "extDnsResolve(", 0, "no DNS call"), "gate must precede the DNS lookup");
    TEST_ASSERT_TRUE_MESSAGE(gate < find_after(body, "UdpExtern.begin(", 0, "no begin()"), "gate must precede begin()");
    TEST_ASSERT_TRUE_MESSAGE(gate < find_after(body, "hasExternIPaddress=true;", 0, "no latch"), "gate must precede the latch");
}

// startExternUDP(): a literal-IP parse failure (RAK: "mcapp.local" passes the target check) must not latch with
// apip still on the old peer.
static void test_wiring_start_extern_udp_checks_the_literal_ip_parse(void)
{
    const std::string src = read_repo_file("src/extudp_functions.cpp");
    const std::string body = function_body(src, "void startExternUDP()\n{");
    TEST_ASSERT_TRUE_MESSAGE(body.find("\n    apip.fromString(str_ip);") == std::string::npos, "apip.fromString() result is ignored again");
    const size_t parse = find_after(body, "if(!apip.fromString(str_ip))", 0, "apip.fromString() result is not tested");
    const size_t ret = find_after(body, "return;", parse, "parse failure does not return");
    const size_t stop = find_after(body, "UdpExtern.stop();", parse, "a failed parse keeps the socket bound");
    TEST_ASSERT_TRUE_MESSAGE(stop < ret, "UdpExtern.stop() must come before the return of the parse failure");
    const size_t latch = find_after(body, "hasExternIPaddress=true;", 0, "no latch");
    TEST_ASSERT_TRUE_MESSAGE(ret < latch, "a failed parse must return before the latch");
    TEST_ASSERT_TRUE_MESSAGE(ret - parse < 500, "the parse failure block is too long to be a plain return");
}

// The "no WiFI connection" line is reached on every pass while the link is down: rate-limited.
static void test_wiring_no_wifi_log_is_rate_limited(void)
{
    const std::string src = read_repo_file("src/extudp_functions.cpp");
    const std::string body = function_body(src, "void startExternUDP()\n{");
    const size_t msg = find_after(body, "[EXT] no WiFI connection open", 0, "no-WiFi message is gone");
    TEST_ASSERT_TRUE_MESSAGE(body.rfind("30000", msg) != std::string::npos && msg - body.rfind("30000", msg) < 300,
                             "the no-WiFi message is not under a 30 s limiter");
}

// WEB-PIN-01: the web handler validates the port (and refuses an input pin) before --setout runs.
static void test_wiring_web_mcpout_checks_the_port_before_the_command(void)
{
    const std::string src = read_repo_file("src/web_functions/web_setup.cpp");
    const size_t a = find_after(src, "equals(\"mcpout\")", 0, "mcpout branch not found");
    const size_t b = find_after(src, "equals(\"mcpname\")", a, "end of the mcpout branch not found");
    const std::string br = src.substr(a, b - a);
    const size_t chk = find_after(br, "mcpout_check_port(", 0, "mcpout_check_port() is not called");
    const size_t cmd = find_after(br, "commandAction(", 0, "mcpout does not run the command");
    TEST_ASSERT_TRUE_MESSAGE(chk < cmd, "mcpout_check_port() must come before commandAction()");
    const size_t fail = find_after(br, "WS_RETURNCODE_FAIL", chk, "no FAIL answer after the check");
    TEST_ASSERT_TRUE_MESSAGE(fail < cmd, "the refusal must be answered before commandAction()");
}

// TD-34: switching BLE from the console sets a dirty flag; the loop (the only task that may touch LVGL) repaints.
static void test_wiring_ble_header_repaint_goes_through_the_loop(void)
{
    const std::string src = read_repo_file("src/esp32/esp32_main.cpp");
    const std::string setter = function_body(src, "void esp32SetBleEnabled(bool on)\n{");
    TEST_ASSERT_TRUE_MESSAGE(setter.find("s_bleHeaderDirty = true;") != std::string::npos, "esp32SetBleEnabled() does not set the dirty flag");
    TEST_ASSERT_TRUE_MESSAGE(setter.find("tdeck_update_header_bt") == std::string::npos, "esp32SetBleEnabled() must not call LVGL itself");
    const std::string loop = function_body(src, "void esp32loop()\n{");
    const size_t c = find_after(loop, "if(s_bleHeaderDirty)", 0, "esp32loop() does not consume the flag");
    const std::string blk = loop.substr(c, 260);
    const size_t clr = find_after(blk, "s_bleHeaderDirty = false;", 0, "the flag is not cleared in the loop");
    const size_t upd = find_after(blk, "tdeck_update_header_bt();", clr, "the loop does not repaint the header next to the flag");
    TEST_ASSERT_TRUE(clr < upd);
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
    RUN_TEST(test_wiring_get_extern_udp_consumes_the_peer_refresh_flag);
    RUN_TEST(test_wiring_extudpip_requests_a_peer_refresh_on_both_paths);
    RUN_TEST(test_wiring_start_extern_udp_has_the_empty_field_gate);
    RUN_TEST(test_wiring_start_extern_udp_checks_the_literal_ip_parse);
    RUN_TEST(test_wiring_no_wifi_log_is_rate_limited);
    RUN_TEST(test_wiring_web_mcpout_checks_the_port_before_the_command);
    RUN_TEST(test_wiring_ble_header_repaint_goes_through_the_loop);
    RUN_TEST(test_error_and_done_json);
    RUN_TEST(test_limiter);
    return UNITY_END();
}
