// Native test suite for decodeTinyXML() (src/tinyxml_functions.cpp), PT-01
// (docs/BACKLOG.md Sec.3.8j): the OTT netDL 500 measuring-station XML that
// arrives over soft-serial and fills strTELE_PARM / strTELE_UNIT /
// strTELE_VALUES / strTELE_DATETIME / strTELE_CH_ID / strTELE_UTCOFF /
// lTELE_TIMER (extern in src/loop_functions_extern.h, defined for real in
// src/softser_functions.cpp -- that file is not linked into this env, so
// this suite provides the definitions itself, see below).
//
// Sample document: no netDL 500 XML sample exists anywhere else in this
// repo (checked docs/, src/, test/, tools/) -- the *only* concrete example
// is the one already embedded, commented out, in src/tinyxml_functions.cpp
// itself (lines 17-81, "only for testing" / testTinyXML()). kSampleDocument
// below reproduces that example's structure and values (three channels --
// Wasserstand/Wassertemperatur/Batteriespannung -- under a
// "DemoStationNetDL500" / "OTT netDL 500" station), collapsed to one line;
// insignificant inter-tag whitespace does not affect what the parser reads.
// TLM-03 (Sec.3.8i) notes the firmware path that calls decodeTinyXML() is
// dead on nodes without a measuring station attached -- the parser is still
// worth pinning on its own.
//
//   pio test -e native_xml
//
// Two real defects were found while writing this suite (both PT-01 findings
// 7 and 8, since fixed in src/tinyxml_functions.cpp -- see
// test_vt_without_text_gets_zero_placeholder and
// test_node_utcoff_converts_parsed_timezone below): an uninitialized-float
// read when a <VT> has no numeric text, and meshcom_settings.node_utcoff
// being hardcoded to 0.0 regardless of the parsed station timezone.

#include <unity.h>

#include <cstring>
#include <string>

#include <Arduino.h>
#include <configuration.h>
#include <loop_functions.h>
#include <loop_functions_extern.h>
#include <tinyxml_functions.h>
#include <nrf52/WisBlock-API.h>   // shim from stubs/: s_meshcom_settings (node_parm_1 etc.)

// ---- Link stubs -------------------------------------------------------
// build_src_filter links only tinyxml_functions.cpp (platformio.ini
// [env:native_xml]), so the globals it reads/writes via `extern` need a
// definition somewhere in this translation unit -- normally
// src/softser_functions.cpp, not linked here.
s_meshcom_settings meshcom_settings;
bool bSOFTSERDEBUG = false;
String strSOFTSERAPP_ID = "";
String strSOFTSERAPP_NAME = "";

String strTELE_PARM = "";
String strTELE_UNIT = "";
String strTELE_VALUES = "";
String strTELE_DATETIME = "";
String strTELE_CH_ID = "";
String strTELE_UTCOFF = "";
unsigned long lTELE_TIMER = 0;

// ---- Sample document (see file header) --------------------------------
static const char *kSampleDocument =
    "<StationDataList>"
    "<StationData stationId=\"0077234567\" name=\"DemoStationNetDL500\" timezone=\"+01:00\">"
    "<ChannelData channelId=\"0060\" name=\"Wasserstand\" unit=\"cm\">"
    "<Values>"
    "<VT t=\"2025-04-22T12:00:00\">28.3</VT>"
    "<VT t=\"2025-04-22T12:05:00\">28.4</VT>"
    "<VT t=\"2025-04-22T13:00:00\">30.1</VT>"
    "</Values>"
    "</ChannelData>"
    "<ChannelData channelId=\"0065\" name=\"Wassertemperatur\" unit=\"&#176;C\">"
    "<Values>"
    "<VT t=\"2025-04-22T12:05:00\">22.0</VT>"
    "<VT t=\"2025-04-22T13:00:00\">22.7</VT>"
    "</Values>"
    "</ChannelData>"
    "<ChannelData channelId=\"0050\" name=\"Batteriespannung\" unit=\"V\">"
    "<Values>"
    "<VT t=\"2025-04-22T12:00:00\">13.2</VT>"
    "<VT t=\"2025-04-22T13:00:00\">12.9</VT>"
    "</Values>"
    "</ChannelData>"
    "</StationData>"
    "</StationDataList>";

void setUp(void)
{
    mc_test_set_millis(0);

    strTELE_PARM = "";
    strTELE_UNIT = "";
    strTELE_VALUES = "";
    strTELE_DATETIME = "";
    strTELE_CH_ID = "";
    strTELE_UTCOFF = "";
    lTELE_TIMER = 0;

    strSOFTSERAPP_ID = "";
    strSOFTSERAPP_NAME = "";

    meshcom_settings = s_meshcom_settings();
    bSOFTSERDEBUG = false;
}

void tearDown(void) {}

// ---------------------------------------------------------------------
// 1. Well-formed document, multiple channels: every global holds the
//    expected value, meshcom_settings mirrors it, and the function
//    returns true. Also pins: only the LAST <VT> in each <Values> block
//    ends up in strTELE_VALUES/strTELE_DATETIME -- decodeTinyXML() starts
//    at LastChildElement("VT") and then calls NextSiblingElement("VT"),
//    which is null for the last element, so the loop body runs exactly
//    once per <Values> block. Earlier <VT> readings (28.3/28.4, 22.0,
//    13.2 above) are silently discarded. Deterministic, not a crash --
//    documented here as real behaviour, not escalated as a defect.
static void test_wellformed_multichannel_fills_all_globals(void)
{
    mc_test_set_millis(4242);

    bool r = decodeTinyXML(String(kSampleDocument));

    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL_UINT32(4242, lTELE_TIMER);

    TEST_ASSERT_EQUAL_STRING("0077234567", strSOFTSERAPP_ID.c_str());
    TEST_ASSERT_EQUAL_STRING("DemoStationNetDL500", strSOFTSERAPP_NAME.c_str());

    // raw timezone attribute, unmodified -- see
    // test_node_utcoff_converts_parsed_timezone below for the converted
    // meshcom_settings.node_utcoff value derived from this string
    TEST_ASSERT_EQUAL_STRING("+01:00", strTELE_UTCOFF.c_str());

    TEST_ASSERT_EQUAL_STRING(
        "60 Wasserstand,65 Wassertemperatur,50 Batteriespannung",
        strTELE_PARM.c_str());
    TEST_ASSERT_EQUAL_STRING("60,65,50", strTELE_CH_ID.c_str());
    // &#176; decoded to the actual degree sign (UTF-8 C2 B0) by tinyxml2
    TEST_ASSERT_EQUAL_STRING("cm,\xC2\xB0"
                              "C,V",
                              strTELE_UNIT.c_str());

    // only the last VT per channel (see comment above)
    TEST_ASSERT_EQUAL_STRING("30.1,22.7,12.9", strTELE_VALUES.c_str());
    // set once, from the first channel processed
    TEST_ASSERT_EQUAL_STRING("2025-04-22T13:00:00", strTELE_DATETIME.c_str());

    TEST_ASSERT_EQUAL_STRING(
        "60 Wasserstand,65 Wassertemperatur,50 Batteriespannung",
        meshcom_settings.node_parm_1);
    TEST_ASSERT_EQUAL_STRING("cm,\xC2\xB0"
                              "C,V",
                              meshcom_settings.node_unit);
    TEST_ASSERT_EQUAL_STRING("T:30.1,22.7,12.9", meshcom_settings.node_values);
    TEST_ASSERT_EQUAL_STRING("2025-04-22T13:00:00", meshcom_settings.node_parm_t);
    TEST_ASSERT_EQUAL_STRING("60,65,50", meshcom_settings.node_parm_id);
}

// ---------------------------------------------------------------------
// 2. A document longer than the fixed meshcom_settings buffers: every
//    field is written with snprintf(buf, sizeof(buf), "%s", ...), which
//    truncates instead of overrunning. Proves that bound holds for the
//    two fields most directly reachable from one long attribute value
//    (node_parm_1 via a long channel name, node_parm_t via a long
//    datetime) plus node_unit; all five node_* string fields in the
//    function follow the identical snprintf pattern.
static void test_document_exceeds_buffer_fields_truncate_without_overrun(void)
{
    const std::string longName(150, 'X');
    const std::string longUnit(80, 'Y');
    const std::string longDatetime =
        "2025-04-22T13:00:00-EXTRA-LONG-SUFFIX-THAT-DOES-NOT-FIT";

    const std::string doc =
        "<StationDataList>"
        "<StationData stationId=\"S1\" name=\"N1\" timezone=\"+00:00\">"
        "<ChannelData channelId=\"0099\" name=\"" +
        longName + "\" unit=\"" + longUnit +
        "\">"
        "<Values><VT t=\"" +
        longDatetime +
        "\">42.7</VT></Values>"
        "</ChannelData>"
        "</StationData>"
        "</StationDataList>";

    bool r = decodeTinyXML(String(doc));
    TEST_ASSERT_TRUE(r);

    // node_parm_1[100]: "99 " + longName, truncated to 99 chars + NUL
    const std::string expectedParm1 = ("99 " + longName).substr(0, 99);
    TEST_ASSERT_EQUAL_STRING(expectedParm1.c_str(), meshcom_settings.node_parm_1);
    TEST_ASSERT_EQUAL_size_t(99, strlen(meshcom_settings.node_parm_1));

    // node_parm_t[25]: truncated to 24 chars + NUL
    const std::string expectedParmT = longDatetime.substr(0, 24);
    TEST_ASSERT_EQUAL_STRING(expectedParmT.c_str(), meshcom_settings.node_parm_t);
    TEST_ASSERT_EQUAL_size_t(24, strlen(meshcom_settings.node_parm_t));

    // node_unit[50]: truncated to 49 chars + NUL
    const std::string expectedUnit = longUnit.substr(0, 49);
    TEST_ASSERT_EQUAL_STRING(expectedUnit.c_str(), meshcom_settings.node_unit);
    TEST_ASSERT_EQUAL_size_t(49, strlen(meshcom_settings.node_unit));

    // unaffected fields prove the truncation is local to the long ones
    TEST_ASSERT_EQUAL_STRING("T:42.7", meshcom_settings.node_values);
    TEST_ASSERT_EQUAL_STRING("99", meshcom_settings.node_parm_id);
}

// ---------------------------------------------------------------------
// 3. StationData with no attributes and no <ChannelData> children at all:
//    Attribute() returns NULL for every missing attribute: assigning that
//    straight to a String (strSOFTSERAPP_ID = station->Attribute(...))
//    does not crash (String's const-char* constructor/assign treats NULL
//    as ""). The <ChannelData> loop body never runs. Actual behaviour:
//    the function still returns TRUE (the document parses fine) with the
//    six strTELE_* telemetry fields cleared to "" -- not "false", despite
//    "missing channel" sounding like a parse failure.
static void test_missing_channeldata_and_attributes_returns_true_cleared(void)
{
    bool r = decodeTinyXML(String("<StationDataList><StationData></StationData></StationDataList>"));

    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL_STRING("", strSOFTSERAPP_ID.c_str());
    TEST_ASSERT_EQUAL_STRING("", strSOFTSERAPP_NAME.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_UTCOFF.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_PARM.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_UNIT.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_VALUES.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_DATETIME.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_CH_ID.c_str());

    TEST_ASSERT_EQUAL_STRING("", meshcom_settings.node_parm_1);
    TEST_ASSERT_EQUAL_STRING("", meshcom_settings.node_unit);
    // "T:" prefix is unconditional, even with an empty value list
    TEST_ASSERT_EQUAL_STRING("T:", meshcom_settings.node_values);
    TEST_ASSERT_EQUAL_STRING("", meshcom_settings.node_parm_t);
    TEST_ASSERT_EQUAL_STRING("", meshcom_settings.node_parm_id);
}

// ---------------------------------------------------------------------
// 4. ChannelData with no channelId attribute and no <Values> child at
//    all ("missing value"): channelId->substring(2) on the empty string
//    NULL-Attribute() produced is itself "" (safe), so strTELE_PARM ends
//    up with a leading space (empty-id + " " + name) -- pinned as actual
//    output, not asserted as "correct". strTELE_VALUES/DATETIME are never
//    touched because the <Values> loop body never runs; the function
//    still returns true.
static void test_channel_missing_id_and_values_stays_safe(void)
{
    bool r = decodeTinyXML(String(
        "<StationDataList><StationData stationId=\"S1\" name=\"N1\" timezone=\"+00:00\">"
        "<ChannelData name=\"NoIdChannel\"></ChannelData>"
        "</StationData></StationDataList>"));

    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL_STRING(" NoIdChannel", strTELE_PARM.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_CH_ID.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_UNIT.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_VALUES.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_DATETIME.c_str());
}

// ---------------------------------------------------------------------
// 5. A well-formed document that simply is not a <StationDataList> with
//    <StationData> children: tinyxml2 parses it fine (XML_SUCCESS), so
//    decodeTinyXML() still clears the six strTELE_* fields and returns
//    true -- the clear happens unconditionally right after a successful
//    parse, before the (here: zero-iteration) <StationData> loop runs.
//    Fields only ever written *inside* that loop (strSOFTSERAPP_ID/NAME)
//    are left holding whatever a previous call put there.
static void test_wellformed_nonmatching_root_clears_telemetry_returns_true(void)
{
    TEST_ASSERT_TRUE(decodeTinyXML(String(kSampleDocument)));
    TEST_ASSERT_EQUAL_STRING("0077234567", strSOFTSERAPP_ID.c_str()); // baseline

    bool r = decodeTinyXML(String("<Foo/>"));

    TEST_ASSERT_TRUE(r);
    // untouched: <Foo/> has no <StationData>, so the assigning loop never runs
    TEST_ASSERT_EQUAL_STRING("0077234567", strSOFTSERAPP_ID.c_str());
    // cleared unconditionally after the successful parse
    TEST_ASSERT_EQUAL_STRING("", strTELE_PARM.c_str());
    TEST_ASSERT_EQUAL_STRING("", strTELE_VALUES.c_str());
    TEST_ASSERT_EQUAL_STRING("", meshcom_settings.node_parm_1);
}

// ---------------------------------------------------------------------
// 6/7/8. Documents tinyxml2 cannot parse at all (empty string, plain
// text, truncated mid-tag): decodeTinyXML() returns false *before*
// clearing anything, so a previous successful call's globals -- both the
// String fields and the meshcom_settings buffers -- are left exactly as
// they were. lTELE_TIMER is the one exception: `lTELE_TIMER = millis()`
// runs unconditionally at the top of the function, before the parse is
// even attempted, so it updates even on a failed parse.
static void assertRejectedAndStatePreserved(const char *label, const String &badDoc)
{
    TEST_ASSERT_TRUE_MESSAGE(decodeTinyXML(String(kSampleDocument)), label);
    mc_test_set_millis(999);

    bool r = decodeTinyXML(badDoc);

    TEST_ASSERT_FALSE_MESSAGE(r, label);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(999, lTELE_TIMER, label); // still updates
    TEST_ASSERT_EQUAL_STRING_MESSAGE("0077234567", strSOFTSERAPP_ID.c_str(), label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "60 Wasserstand,65 Wassertemperatur,50 Batteriespannung",
        strTELE_PARM.c_str(), label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("30.1,22.7,12.9", strTELE_VALUES.c_str(), label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "60 Wasserstand,65 Wassertemperatur,50 Batteriespannung",
        meshcom_settings.node_parm_1, label);
}

static void test_empty_document_returns_false_and_preserves_state(void)
{
    assertRejectedAndStatePreserved("empty document", String(""));
}

static void test_nonxml_text_returns_false_and_preserves_state(void)
{
    assertRejectedAndStatePreserved("non-XML text", String("this is not XML at all"));
}

static void test_truncated_xml_returns_false_and_preserves_state(void)
{
    assertRejectedAndStatePreserved(
        "truncated mid-tag",
        String("<StationDataList><StationData stationId=\"S1\""));
}

// ---------------------------------------------------------------------
// 9. An embedded NUL inside the document: XMLDocument::Parse() is handed
//    document.c_str() with no explicit length, so it reads a plain
//    C-string and silently stops at the first NUL byte. A well-formed
//    prefix up to that point still parses and populates the globals
//    normally; whatever follows the NUL is never seen by the parser (no
//    crash, no overrun -- but also no diagnostic that the document was
//    cut short).
static void test_embedded_nul_truncates_parse_silently(void)
{
    std::string doc =
        "<StationDataList><StationData stationId=\"S1\" name=\"N1\" timezone=\"+00:00\">"
        "<ChannelData channelId=\"0011\" name=\"Ch\" unit=\"U\">"
        "<Values><VT t=\"2025-01-01T00:00:00\">5.0</VT></Values>"
        "</ChannelData></StationData></StationDataList>";
    doc.push_back('\0');
    doc += "GARBAGE-AFTER-THE-NUL-MUST-STAY-INVISIBLE-TO-THE-PARSER";

    bool r = decodeTinyXML(String(doc));

    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL_STRING("S1", strSOFTSERAPP_ID.c_str());
    TEST_ASSERT_EQUAL_STRING("11", strTELE_CH_ID.c_str());
    TEST_ASSERT_EQUAL_STRING("5.0", strTELE_VALUES.c_str());
}

// ---------------------------------------------------------------------
// PT-01 finding 7, fixed: a <VT> element with no numeric text (missing,
// self-closed, or non-numeric) makes tinyxml2's QueryFloatText() return
// XML_NO_TEXT_NODE / XML_CAN_NOT_CONVERT_TEXT *without touching its output
// parameter* -- confirmed against the downloaded tinyxml2 directly:
// `float val; val = 987.65f; vt->QueryFloatText(&val);` on an empty <VT>
// leaves val == 987.65 afterwards. decodeTinyXML() now checks the return
// code and substitutes an explicit 0.0 placeholder instead of formatting
// whatever was on the stack -- a plain skip would have shifted every later
// channel's value out of positional alignment with strTELE_PARM, so this
// pins both effects: the placeholder value itself, and that a *second*,
// well-formed channel after the bad one still lines up correctly.
static void test_vt_without_text_gets_zero_placeholder(void)
{
    Serial.clear();

    bool r = decodeTinyXML(String(
        "<StationDataList><StationData stationId=\"S1\" name=\"N1\" timezone=\"+00:00\">"
        "<ChannelData channelId=\"0011\" name=\"Ch1\" unit=\"U\">"
        "<Values><VT t=\"2025-01-01T00:00:00\"></VT></Values>"
        "</ChannelData>"
        "<ChannelData channelId=\"0022\" name=\"Ch2\" unit=\"U\">"
        "<Values><VT t=\"2025-01-01T00:05:00\">1.5</VT></Values>"
        "</ChannelData>"
        "</StationData></StationDataList>"));
    TEST_ASSERT_TRUE(r);

    // placeholder for the empty <VT>, real value for the next channel --
    // both entries present, so PARM/VALUES stay positionally paired
    TEST_ASSERT_EQUAL_STRING("0.0,1.5", strTELE_VALUES.c_str());
    TEST_ASSERT_EQUAL_STRING("11 Ch1,22 Ch2", strTELE_PARM.c_str());
    TEST_ASSERT_EQUAL_STRING("T:0.0,1.5", meshcom_settings.node_values);

    // one raw diagnostic line, not filtered by printfdeb/--debug
    TEST_ASSERT_TRUE(Serial.captured().find("[TXML];vt;novalue") != std::string::npos);
}

// ---------------------------------------------------------------------
// PT-01 finding 8, fixed: the station's parsed timezone now reaches
// meshcom_settings.node_utcoff. strTELE_UTCOFF is filled from the
// "timezone" attribute (see test 1 above); decodeTinyXML() converts it into
// decimal hours ("+01:00" -> 1.0, "-03:30" -> -3.5), converting hours and
// minutes separately rather than the old `replace(":", ".")` + toFloat()
// approach, which would have produced -3.30 for -03:30, not -3.5. When the
// attribute is absent (or unparseable), node_utcoff is left untouched
// instead of being reset -- pinned here by re-parsing a document with no
// timezone attribute after a good one and checking the prior value survives.
static void test_node_utcoff_converts_parsed_timezone(void)
{
    bool r = decodeTinyXML(String(kSampleDocument)); // timezone="+01:00" in the document
    TEST_ASSERT_TRUE(r);
    TEST_ASSERT_EQUAL_STRING("+01:00", strTELE_UTCOFF.c_str());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, meshcom_settings.node_utcoff);

    // half-hour, negative offset
    bool r2 = decodeTinyXML(String(
        "<StationDataList><StationData stationId=\"S2\" name=\"N2\" timezone=\"-03:30\">"
        "<ChannelData channelId=\"0011\" name=\"Ch\" unit=\"U\">"
        "<Values><VT t=\"2025-01-01T00:00:00\">5.0</VT></Values>"
        "</ChannelData></StationData></StationDataList>"));
    TEST_ASSERT_TRUE(r2);
    TEST_ASSERT_EQUAL_STRING("-03:30", strTELE_UTCOFF.c_str());
    TEST_ASSERT_EQUAL_FLOAT(-3.5f, meshcom_settings.node_utcoff);

    // no timezone attribute at all: strTELE_UTCOFF comes back empty, and
    // node_utcoff must stay at the previous good value (-3.5), not reset
    bool r3 = decodeTinyXML(String(
        "<StationDataList><StationData stationId=\"S3\" name=\"N3\">"
        "<ChannelData channelId=\"0011\" name=\"Ch\" unit=\"U\">"
        "<Values><VT t=\"2025-01-01T00:00:00\">7.0</VT></Values>"
        "</ChannelData></StationData></StationDataList>"));
    TEST_ASSERT_TRUE(r3);
    TEST_ASSERT_EQUAL_STRING("", strTELE_UTCOFF.c_str());
    TEST_ASSERT_EQUAL_FLOAT(-3.5f, meshcom_settings.node_utcoff);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_wellformed_multichannel_fills_all_globals);
    RUN_TEST(test_document_exceeds_buffer_fields_truncate_without_overrun);
    RUN_TEST(test_missing_channeldata_and_attributes_returns_true_cleared);
    RUN_TEST(test_channel_missing_id_and_values_stays_safe);
    RUN_TEST(test_wellformed_nonmatching_root_clears_telemetry_returns_true);
    RUN_TEST(test_empty_document_returns_false_and_preserves_state);
    RUN_TEST(test_nonxml_text_returns_false_and_preserves_state);
    RUN_TEST(test_truncated_xml_returns_false_and_preserves_state);
    RUN_TEST(test_embedded_nul_truncates_parse_silently);
    RUN_TEST(test_vt_without_text_gets_zero_placeholder);
    RUN_TEST(test_node_utcoff_converts_parsed_timezone);
    return UNITY_END();
}
