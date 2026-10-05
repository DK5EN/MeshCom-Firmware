// Host test for src/fw_update.h -- pure logic of the firmware auto update (#1187).
//
//   pio test -e native_fw_update
//
// Covers: tag parsing, the version/date comparator (AU-D10), the channel
// repos (AU-D2), the staging layout (AU-D11), the NVS stage record, and the
// check/handover policy with its timer, jitter and retry limits (AU-D1..D5).

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include <fw_update.h>

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------- helpers

static FwVersion V(const char *tag)
{
    FwVersion v;
    TEST_ASSERT_TRUE_MESSAGE(fwParseTag(tag, v), tag);
    return v;
}

// cand vs running by tag text
static int cmp(const char *running, const char *cand) { return fwCompare(V(running), V(cand)); }

// ------------------------------------------------------------ tag parsing

static void test_parse_plain(void)
{
    FwVersion v;
    TEST_ASSERT_TRUE(fwParseTag("v4.40a", v));
    TEST_ASSERT_TRUE(v.valid);
    TEST_ASSERT_EQUAL_UINT8(4, v.major);
    TEST_ASSERT_EQUAL_UINT8(40, v.minor);
    TEST_ASSERT_EQUAL_CHAR('a', v.letter);
    TEST_ASSERT_FALSE(v.hasDate);
}

static void test_parse_dated(void)
{
    FwVersion v;
    TEST_ASSERT_TRUE(fwParseTag("v4.40a.10.02", v));
    TEST_ASSERT_TRUE(v.hasDate);
    TEST_ASSERT_EQUAL_UINT8(10, v.month);
    TEST_ASSERT_EQUAL_UINT8(2, v.day);
    TEST_ASSERT_EQUAL_UINT8(4, v.major);
    TEST_ASSERT_EQUAL_UINT8(40, v.minor);
}

static void test_parse_without_v(void)
{
    FwVersion v;
    TEST_ASSERT_TRUE(fwParseTag("4.35v.09.30", v));
    TEST_ASSERT_EQUAL_UINT8(4, v.major);
    TEST_ASSERT_EQUAL_UINT8(35, v.minor);
    TEST_ASSERT_EQUAL_CHAR('v', v.letter);
    TEST_ASSERT_EQUAL_UINT8(9, v.month);
    TEST_ASSERT_EQUAL_UINT8(30, v.day);
    TEST_ASSERT_TRUE(fwParseTag("4.40z", v));
    TEST_ASSERT_EQUAL_CHAR('z', v.letter);
}

static void test_parse_date_edges(void)
{
    FwVersion v;
    TEST_ASSERT_TRUE(fwParseTag("v4.40a.01.01", v));
    TEST_ASSERT_TRUE(fwParseTag("v4.40a.12.31", v));
    TEST_ASSERT_TRUE(fwParseTag("v4.40a.2.3", v)); // one-digit month and day
    TEST_ASSERT_EQUAL_UINT8(2, v.month);
    TEST_ASSERT_EQUAL_UINT8(3, v.day);
    TEST_ASSERT_TRUE(fwParseTag("v4.40a.02.29", v)); // leap day is a valid tag
}

static void test_parse_invalid(void)
{
    static const char *bad[] = {
        "",           "v",           "v4",          "v4.",         "v4.40",        "v4.40A",
        "v4.40aa",    "v4.40a.",     "v4.40a.10",   "v4.40a.10.",  "v4.40a.10.02.", "v4.40a.13.01",
        "v4.40a.00.10", "v4.40a.10.00", "v4.40a.10.32", "v4.40a.04.31", "v4.40a.02.30", "v4.40a-neo",
        "v4.40a ",    " v4.40a",     "vv4.40a",     "v.40a",       "v4.a",         "v4.40@",
        "v256.1a",    "v4.256a",     "v4.1000a",    "v4.40a.1x.02", "x4.40a",      "v4,40a",
        "latest",     "v4.40a\n",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        FwVersion v;
        v.valid = true;
        TEST_ASSERT_FALSE_MESSAGE(fwParseTag(bad[i], v), bad[i]);
        TEST_ASSERT_FALSE_MESSAGE(v.valid, bad[i]);
    }
    FwVersion v;
    TEST_ASSERT_FALSE(fwParseTag(nullptr, v));
    TEST_ASSERT_FALSE(v.valid);
}

// ---------------------------------------------------------------- compare

static void test_compare_version_order(void)
{
    TEST_ASSERT_TRUE(cmp("v4.35v", "v4.40a") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a", "v4.40b") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40b", "v4.41a") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a", "v4.35v") < 0);
    TEST_ASSERT_TRUE(cmp("v4.40b", "v4.40a") < 0);
    TEST_ASSERT_TRUE(cmp("v4.41a", "v4.40b") < 0);
    TEST_ASSERT_TRUE(cmp("v4.99z", "v5.00a") > 0);  // major wins
    TEST_ASSERT_TRUE(cmp("v5.00a", "v4.99z") < 0);
    TEST_ASSERT_EQUAL_INT(0, cmp("v4.40a", "v4.40a"));
}

static void test_compare_version_beats_date(void)
{
    // a later letter wins whatever the date says
    TEST_ASSERT_TRUE(cmp("v4.40a.10.05", "v4.40b.10.01") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40b.01.01", "v4.40a.12.31") < 0);
}

static void test_compare_same_version_date(void)
{
    TEST_ASSERT_TRUE(cmp("v4.40a.10.02", "v4.40a.10.05") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a.10.05", "v4.40a.10.02") < 0);
    TEST_ASSERT_EQUAL_INT(0, cmp("v4.40a.10.02", "v4.40a.10.02"));
}

static void test_compare_new_year(void)
{
    TEST_ASSERT_TRUE(cmp("v4.40a.12.30", "v4.40a.01.03") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a.01.03", "v4.40a.12.30") < 0);
}

static void test_compare_half_year_window(void)
{
    // running 01.01: +182 days is 07.02 (newer), +183 days is 07.03 (older)
    TEST_ASSERT_TRUE(cmp("v4.40a.01.01", "v4.40a.07.02") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a.01.01", "v4.40a.07.03") < 0);
    // one day after / before
    TEST_ASSERT_TRUE(cmp("v4.40a.01.01", "v4.40a.01.02") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a.01.02", "v4.40a.01.01") < 0);
    TEST_ASSERT_TRUE(cmp("v4.40a.01.01", "v4.40a.12.31") < 0); // one day BEFORE
}

static void test_compare_antisymmetric_every_day(void)
{
    // For every pair of day-of-year values the result is exactly mirrored,
    // except the equal case, which is 0 both ways.
    static const uint8_t md[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    FwVersion a, b;
    a = V("v4.40a.01.01");
    b = V("v4.40a.01.01");
    for (uint8_t m1 = 1; m1 <= 12; m1++)
        for (uint8_t d1 = 1; d1 <= md[m1 - 1]; d1 += 3)
            for (uint8_t m2 = 1; m2 <= 12; m2++)
                for (uint8_t d2 = 1; d2 <= md[m2 - 1]; d2 += 5)
                {
                    a.month = m1;
                    a.day = d1;
                    b.month = m2;
                    b.day = d2;
                    int ab = fwCompare(a, b);
                    int ba = fwCompare(b, a);
                    TEST_ASSERT_EQUAL_INT(-ba, ab);
                }
}

static void test_compare_leap_day_is_feb28(void)
{
    TEST_ASSERT_EQUAL_INT(0, cmp("v4.40a.02.28", "v4.40a.02.29"));
    TEST_ASSERT_TRUE(cmp("v4.40a.02.29", "v4.40a.03.01") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a.03.01", "v4.40a.02.29") < 0);
}

static void test_compare_dated_vs_undated(void)
{
    TEST_ASSERT_TRUE(cmp("v4.40a", "v4.40a.10.02") > 0);  // dated cand, undated running
    TEST_ASSERT_TRUE(cmp("v4.40a.10.02", "v4.40a") < 0);  // undated cand, dated running
    TEST_ASSERT_EQUAL_INT(0, cmp("v4.40a", "v4.40a"));    // both undated
    // an undated higher version is still newer than a dated lower one
    TEST_ASSERT_TRUE(cmp("v4.40a.10.02", "v4.40b") > 0);
}

static void test_compare_fork_vs_upstream_same_letter(void)
{
    // upstream release and a fork release of the same letter: the date decides
    TEST_ASSERT_TRUE(cmp("v4.40a.10.02", "v4.40a.10.20") > 0);
    TEST_ASSERT_TRUE(cmp("v4.40a.10.20", "v4.40a.10.02") < 0);
    // upstream tags without a date: an undated upstream tag never beats a dated fork build
    TEST_ASSERT_TRUE(cmp("v4.40a.10.20", "v4.40a") < 0);
}

static void test_compare_invalid_is_zero(void)
{
    FwVersion bad;
    TEST_ASSERT_FALSE(fwParseTag("junk", bad));
    FwVersion ok = V("v4.40a");
    TEST_ASSERT_EQUAL_INT(0, fwCompare(bad, ok));
    TEST_ASSERT_EQUAL_INT(0, fwCompare(ok, bad));
    TEST_ASSERT_EQUAL_INT(0, fwCompare(bad, bad));
}

// ---------------------------------------------------------------- channel

static void test_channel_repos(void)
{
    TEST_ASSERT_EQUAL_STRING("icssw-org/MeshCom-Firmware", fwChannelRepo(FW_CH_PROD));
    TEST_ASSERT_EQUAL_STRING("DK5EN/MeshCom-Firmware", fwChannelRepo(FW_CH_DEV));
    TEST_ASSERT_EQUAL_UINT8(0, FW_CH_PROD);
    TEST_ASSERT_EQUAL_UINT8(1, FW_CH_DEV);
    // an out-of-range stored value falls back to prod
    TEST_ASSERT_EQUAL_STRING("icssw-org/MeshCom-Firmware", fwChannelRepo((FwChannel)7));
}

// ----------------------------------------------------------------- layout

#define KB 1024u

// Layout with a harmless inflated length (1 KB, below every valid stage offset).
static FwRefuse lay(uint32_t slot, uint32_t running, uint32_t z, uint32_t &off)
{
    return fwStageLayout(slot, running, z, 1u * KB, off);
}

static void test_layout_ok_3324kb_slot(void)
{
    uint32_t slot = 3324u * KB;           // 3,403,776
    uint32_t running = 1760u * KB;        // about 1.76 MB
    uint32_t z = 1050u * KB;
    uint32_t off = 0;
    TEST_ASSERT_EQUAL_UINT8(FW_OK, fwStageLayout(slot, running, z, running, off));
    TEST_ASSERT_EQUAL_UINT32(0u, off % (64u * KB));
    TEST_ASSERT_TRUE(off <= slot - z);
    TEST_ASSERT_TRUE(slot - z - off < 64u * KB); // rounded down by less than one block
    TEST_ASSERT_TRUE(off >= running);
    // 3324 KB - 1050 KB = 2274 KB -> 2240 KB (35 blocks of 64 KB)
    TEST_ASSERT_EQUAL_UINT32(2240u * KB, off);
}

static void test_layout_exact_alignment(void)
{
    uint32_t off = 0;
    // slot - z exactly on a 64 KB boundary stays
    TEST_ASSERT_EQUAL_UINT8(FW_OK, lay(3328u * KB, 100u * KB, 1024u * KB, off));
    TEST_ASSERT_EQUAL_UINT32(2304u * KB, off);
    // one byte more compressed: the offset drops one whole block
    TEST_ASSERT_EQUAL_UINT8(FW_OK, lay(3328u * KB, 100u * KB, 1024u * KB + 1u, off));
    TEST_ASSERT_EQUAL_UINT32(2240u * KB, off);
}

static void test_layout_overlap(void)
{
    uint32_t off = 0;
    uint32_t slot = 3324u * KB;
    uint32_t z = 1050u * KB; // stage_off 2240 KB
    TEST_ASSERT_EQUAL_UINT8(FW_REF_OVERLAP, lay(slot, 2241u * KB, z, off));
    TEST_ASSERT_EQUAL_UINT32(2240u * KB, off);
    // running image exactly up to stage_off is fine
    TEST_ASSERT_EQUAL_UINT8(FW_OK, lay(slot, 2240u * KB, z, off));
    TEST_ASSERT_EQUAL_UINT8(FW_REF_OVERLAP, lay(slot, 2240u * KB + 1u, z, off));
    // an AU image too big for a big asset
    TEST_ASSERT_EQUAL_UINT8(FW_REF_OVERLAP, lay(slot, 2500u * KB, 1500u * KB, off));
}

static void test_layout_size_refusals(void)
{
    uint32_t off = 123;
    uint32_t slot = 3324u * KB;
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SIZE, lay(slot, 1000u * KB, 0, off));
    TEST_ASSERT_EQUAL_UINT32(0, off);
    off = 123;
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SIZE, lay(slot, 1000u * KB, slot + 1u, off));
    TEST_ASSERT_EQUAL_UINT32(0, off);
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SIZE, lay(slot, 1000u * KB, 0xFFFFFFFFu, off));
}

static void test_layout_small(void)
{
    uint32_t off = 123;
    // asset as big as the slot: stage_off 0
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SMALL, lay(1024u * KB, 0, 1024u * KB, off));
    TEST_ASSERT_EQUAL_UINT32(0, off);
    // tiny slot
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SMALL, lay(32u * KB, 0, 1u * KB, off));
    // slot 130 KB, z 1 KB -> 129 KB rounds to 128 KB: allowed
    TEST_ASSERT_EQUAL_UINT8(FW_OK, lay(130u * KB, 0, 1u * KB, off));
    TEST_ASSERT_EQUAL_UINT32(128u * KB, off);
    // slot 70 KB, z 1 KB -> 69 KB rounds to 64 KB: the smallest allowed offset
    TEST_ASSERT_EQUAL_UINT8(FW_OK, lay(70u * KB, 0, 1u * KB, off));
    TEST_ASSERT_EQUAL_UINT32(64u * KB, off);
    // slot 130 KB, z 70 KB -> 60 KB rounds to 0: small
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SMALL, lay(130u * KB, 0, 70u * KB, off));
    // zero slot
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SIZE, lay(0, 0, 1u, off));
}

static void test_layout_16mb_table(void)
{
    uint32_t off = 0;
    uint32_t slot = 6528u * KB; // typical big ota_0
    TEST_ASSERT_EQUAL_UINT8(FW_OK, lay(slot, 1800u * KB, 1100u * KB, off));
    TEST_ASSERT_EQUAL_UINT32(0u, off % (64u * KB));
}

// ----------------------------------------------------------------- record

static FwStageRecord sampleRecord(void)
{
    FwStageRecord r;
    memset(&r, 0, sizeof(r));
    r.magic = FW_STAGE_MAGIC;
    strcpy(r.tag, "v4.40a.10.02");
    strcpy(r.env, "heltec_wifi_lora_32_V3");
    r.off = 2240u * KB;
    r.zlen = 1050u * KB + 17u;
    r.ilen = 1760u * KB + 5u;
    r.crc32 = 0xDEADBEEFu;
    for (int i = 0; i < 32; i++)
        r.sha256[i] = (uint8_t)(0xA0 + i);
    r.ts = 1791100800u;
    return r;
}

static void test_record_magic_value(void)
{
    // 'FWS2' little-endian: bytes 'F','W','S','2'
    uint8_t buf[FW_STAGE_ENC_LEN];
    FwStageRecord r = sampleRecord();
    TEST_ASSERT_EQUAL_UINT32(FW_STAGE_ENC_LEN, fwRecordEncode(r, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8('F', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('W', buf[1]);
    TEST_ASSERT_EQUAL_UINT8('S', buf[2]);
    TEST_ASSERT_EQUAL_UINT8('2', buf[3]);
}

static void test_record_round_trip(void)
{
    uint8_t buf[FW_STAGE_ENC_LEN + 8];
    memset(buf, 0x55, sizeof(buf));
    FwStageRecord r = sampleRecord();
    TEST_ASSERT_EQUAL_UINT32(FW_STAGE_ENC_LEN, fwRecordEncode(r, buf, sizeof(buf)));
    FwStageRecord o;
    memset(&o, 0xEE, sizeof(o));
    TEST_ASSERT_TRUE(fwRecordDecode(buf, FW_STAGE_ENC_LEN, o));
    TEST_ASSERT_EQUAL_UINT32(FW_STAGE_MAGIC, o.magic);
    TEST_ASSERT_EQUAL_STRING("v4.40a.10.02", o.tag);
    TEST_ASSERT_EQUAL_STRING("heltec_wifi_lora_32_V3", o.env);
    TEST_ASSERT_EQUAL_UINT32(r.off, o.off);
    TEST_ASSERT_EQUAL_UINT32(r.zlen, o.zlen);
    TEST_ASSERT_EQUAL_UINT32(r.ilen, o.ilen);
    TEST_ASSERT_EQUAL_UINT32(r.crc32, o.crc32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(r.sha256, o.sha256, 32);
    TEST_ASSERT_EQUAL_UINT32(r.ts, o.ts);
}

static void test_record_little_endian_layout(void)
{
    uint8_t buf[FW_STAGE_ENC_LEN];
    FwStageRecord r = sampleRecord();
    r.off = 0x01020304u;
    r.zlen = 0x05060708u;
    r.ilen = 0x11121314u;
    r.crc32 = 0x090A0B0Cu;
    r.ts = 0x0D0E0F10u;
    fwRecordEncode(r, buf, sizeof(buf));
    // 4 magic + 24 tag + 32 env = 60
    TEST_ASSERT_EQUAL_UINT8(0x04, buf[60]);
    TEST_ASSERT_EQUAL_UINT8(0x03, buf[61]);
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[62]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[63]);
    TEST_ASSERT_EQUAL_UINT8(0x08, buf[64]); // zlen
    TEST_ASSERT_EQUAL_UINT8(0x05, buf[67]);
    TEST_ASSERT_EQUAL_UINT8(0x14, buf[68]); // ilen
    TEST_ASSERT_EQUAL_UINT8(0x11, buf[71]);
    TEST_ASSERT_EQUAL_UINT8(0x0C, buf[72]); // crc32
    TEST_ASSERT_EQUAL_UINT8(0xA0, buf[76]); // sha256[0]
    TEST_ASSERT_EQUAL_UINT8(0xBF, buf[107]); // sha256[31]
    TEST_ASSERT_EQUAL_UINT8(0x10, buf[108]); // ts
    TEST_ASSERT_EQUAL_UINT8(0x0D, buf[111]);
}

static void test_record_encode_is_deterministic(void)
{
    // garbage after the NUL in the struct must not leak into the encoding
    FwStageRecord a = sampleRecord();
    FwStageRecord b = sampleRecord();
    memset(b.tag + 13, 0x7F, sizeof(b.tag) - 13);
    memset(b.env + 23, 0x7F, sizeof(b.env) - 23);
    uint8_t ba[FW_STAGE_ENC_LEN], bb[FW_STAGE_ENC_LEN];
    fwRecordEncode(a, ba, sizeof(ba));
    fwRecordEncode(b, bb, sizeof(bb));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ba, bb, FW_STAGE_ENC_LEN);
}

static void test_record_small_buffer(void)
{
    uint8_t buf[FW_STAGE_ENC_LEN];
    FwStageRecord r = sampleRecord();
    TEST_ASSERT_EQUAL_UINT32(0, fwRecordEncode(r, buf, FW_STAGE_ENC_LEN - 1));
    TEST_ASSERT_EQUAL_UINT32(0, fwRecordEncode(r, buf, 0));
    TEST_ASSERT_EQUAL_UINT32(0, fwRecordEncode(r, nullptr, 200));
}

static void test_record_unterminated_string_on_encode_is_truncated(void)
{
    FwStageRecord r = sampleRecord();
    memset(r.tag, 'x', sizeof(r.tag));     // no NUL at all
    memset(r.env, 'y', sizeof(r.env));
    uint8_t buf[FW_STAGE_ENC_LEN];
    TEST_ASSERT_EQUAL_UINT32(FW_STAGE_ENC_LEN, fwRecordEncode(r, buf, sizeof(buf)));
    FwStageRecord o;
    TEST_ASSERT_TRUE(fwRecordDecode(buf, sizeof(buf), o));
    TEST_ASSERT_EQUAL_UINT32(23, strlen(o.tag));
    TEST_ASSERT_EQUAL_UINT32(31, strlen(o.env));
}

static void test_record_decode_rejects_bad_magic(void)
{
    uint8_t buf[FW_STAGE_ENC_LEN];
    FwStageRecord r = sampleRecord();
    fwRecordEncode(r, buf, sizeof(buf));
    FwStageRecord o = sampleRecord();
    o.off = 77;
    buf[0] ^= 0x01;
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf), o));
    TEST_ASSERT_EQUAL_UINT32(77, o.off); // untouched
    buf[0] ^= 0x01;
    TEST_ASSERT_TRUE(fwRecordDecode(buf, sizeof(buf), o));
    // all zeros (erased NVS blob)
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf), o));
    memset(buf, 0xFF, sizeof(buf));
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf), o));
}

static void test_record_decode_rejects_wrong_size(void)
{
    uint8_t buf[FW_STAGE_ENC_LEN + 4];
    FwStageRecord r = sampleRecord();
    fwRecordEncode(r, buf, sizeof(buf));
    FwStageRecord o;
    TEST_ASSERT_FALSE(fwRecordDecode(buf, FW_STAGE_ENC_LEN - 1, o)); // truncated
    TEST_ASSERT_FALSE(fwRecordDecode(buf, FW_STAGE_ENC_LEN + 1, o)); // longer
    TEST_ASSERT_FALSE(fwRecordDecode(buf, 0, o));
    TEST_ASSERT_FALSE(fwRecordDecode(nullptr, FW_STAGE_ENC_LEN, o));
}

static void test_record_decode_rejects_unterminated_strings(void)
{
    uint8_t buf[FW_STAGE_ENC_LEN];
    FwStageRecord r = sampleRecord();
    fwRecordEncode(r, buf, sizeof(buf));
    FwStageRecord o;
    memset(buf + 4, 'x', 24); // tag field filled, no NUL
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf), o));
    fwRecordEncode(r, buf, sizeof(buf));
    memset(buf + 28, 'y', 32); // env field filled, no NUL
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf), o));
}

// ----------------------------------------------------------------- policy

static FwPolicyIn okIn(uint8_t mode)
{
    FwPolicyIn in;
    memset(&in, 0, sizeof(in));
    in.mode = mode;
    in.wifiUp = true;
    in.timeValid = true;
    in.phoneConnected = false;
    in.onBattery = false;
    in.battMv = 4100;
    in.localMinuteOfDay = 12 * 60;
    return in;
}

static void test_first_check_after_ten_minutes(void)
{
    FwTimer t;
    fwTimerInit(t, 1000, 42);
    FwPolicyIn in = okIn(1);
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, 1000));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, 1000 + 10u * 60u * 1000u - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, 1000 + 10u * 60u * 1000u));
    // stays due until the caller reports it done
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, 1000 + 11u * 60u * 1000u));
    TEST_ASSERT_FALSE(t.firstDone);
    fwCheckDone(t, 1000 + 11u * 60u * 1000u);
    TEST_ASSERT_TRUE(t.firstDone);
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, 1000 + 12u * 60u * 1000u));
}

static void test_no_check_without_preconditions(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 7);
    uint32_t due = 11u * 60u * 1000u;

    FwPolicyIn in = okIn(0); // mode off
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, due));

    in = okIn(1);
    in.wifiUp = false;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due));

    in = okIn(1);
    in.timeValid = false;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due));

    in = okIn(2);
    in.wifiUp = false;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due));

    in = okIn(1);
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, due));
    in = okIn(2);
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, due));
}

static void test_never_with_phone(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 7);
    uint32_t due = 11u * 60u * 1000u;
    for (uint8_t mode = 0; mode <= 2; mode++)
    {
        FwPolicyIn in = okIn(mode);
        in.phoneConnected = true;
        in.localMinuteOfDay = 4 * 60;
        TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, due));
        TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due));
    }
}

static void test_jitter_bounds_many_seeds(void)
{
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    const uint32_t min_ms = 22u * 3600u * 1000u;
    const uint32_t max_ms = 26u * 3600u * 1000u;
    for (uint32_t seed = 0; seed < 5000; seed++)
    {
        FwTimer t;
        fwTimerInit(t, 123456u, seed);
        for (int round = 0; round < 4; round++)
        {
            uint32_t now = 500000u + (uint32_t)round * 100000u;
            fwCheckDone(t, now);
            uint32_t d = t.nextCheckMs - now;
            TEST_ASSERT_TRUE(d >= min_ms);
            TEST_ASSERT_TRUE(d <= max_ms);
            if (d < lo)
                lo = d;
            if (d > hi)
                hi = d;
        }
    }
    // the spread is actually used, not a constant 24 h
    TEST_ASSERT_TRUE(lo < 22u * 3600u * 1000u + 15u * 60u * 1000u);
    TEST_ASSERT_TRUE(hi > 26u * 3600u * 1000u - 15u * 60u * 1000u);
}

static void test_jitter_differs_between_seeds_and_rounds(void)
{
    FwTimer a, b;
    fwTimerInit(a, 0, 1);
    fwTimerInit(b, 0, 2);
    fwCheckDone(a, 0);
    fwCheckDone(b, 0);
    TEST_ASSERT_TRUE(a.nextCheckMs != b.nextCheckMs);
    uint32_t first = a.nextCheckMs;
    fwCheckDone(a, 0);
    TEST_ASSERT_TRUE(a.nextCheckMs != first);
    // same seed -> same schedule (deterministic)
    FwTimer c;
    fwTimerInit(c, 0, 1);
    fwCheckDone(c, 0);
    TEST_ASSERT_EQUAL_UINT32(first, c.nextCheckMs);
    // seed 0 does not degenerate
    FwTimer z;
    fwTimerInit(z, 0, 0);
    fwCheckDone(z, 0);
    TEST_ASSERT_TRUE(z.nextCheckMs >= 22u * 3600u * 1000u);
    TEST_ASSERT_TRUE(z.nextCheckMs <= 26u * 3600u * 1000u);
}

static void test_check_due_at_next_check(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 99);
    FwPolicyIn in = okIn(1);
    uint32_t now = 11u * 60u * 1000u;
    fwCheckDone(t, now);
    uint32_t next = t.nextCheckMs;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, next - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, next));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, next + 1u));
}

static void test_handover_window_edges(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 5);
    fwCheckDone(t, 0); // no check due, isolate the handover
    uint32_t now = 1000;
    FwPolicyIn in = okIn(2);
    in.localMinuteOfDay = 2 * 60 + 59;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, now));
    in.localMinuteOfDay = 3 * 60;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, now));
    in.localMinuteOfDay = 4 * 60 + 59;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, now));
    in.localMinuteOfDay = 5 * 60;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, now));
    in.localMinuteOfDay = 0;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, now));
    in.localMinuteOfDay = 1439;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, now));
    in.localMinuteOfDay = 4000; // out of range input never matches
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, now));
}

static void test_handover_needs_mode_auto_and_staged(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 5);
    fwCheckDone(t, 0);
    FwPolicyIn in = okIn(2);
    in.localMinuteOfDay = 4 * 60;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, 1000));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, 1000)); // nothing staged
    in.mode = 1;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, 1000)); // notify never hands over
    in.mode = 0;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, 1000));
}

static void test_handover_needs_no_wifi(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 5);
    fwCheckDone(t, 0);
    FwPolicyIn in = okIn(2);
    in.localMinuteOfDay = 4 * 60;
    in.wifiUp = false;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, 1000));
}

static void test_handover_needs_valid_time(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 5);
    fwCheckDone(t, 0);
    FwPolicyIn in = okIn(2);
    in.localMinuteOfDay = 4 * 60;
    in.timeValid = false;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, 1000));
}

static void test_handover_battery_floor(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 5);
    fwCheckDone(t, 0);
    FwPolicyIn in = okIn(2);
    in.localMinuteOfDay = 4 * 60;
    in.onBattery = true;
    in.battMv = 3599;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, 1000));
    in.battMv = 3600;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, 1000));
    in.battMv = 4200;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, 1000));
    // mains: the voltage is irrelevant (even a bogus 0)
    in.onBattery = false;
    in.battMv = 0;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, 1000));
}

static void test_handover_beats_due_check(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 5);
    FwPolicyIn in = okIn(2);
    in.localMinuteOfDay = 4 * 60;
    uint32_t due = 11u * 60u * 1000u;
    TEST_ASSERT_EQUAL_UINT8(FW_HANDOVER, fwTick(t, in, true, due));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, due));
}

static void test_battery_does_not_gate_check(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 5);
    FwPolicyIn in = okIn(1);
    in.onBattery = true;
    in.battMv = 3300;
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, 11u * 60u * 1000u));
}

// ---------------------------------------------------------------- retries

static void test_attempts_three_per_tag(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 1);
    const uint32_t H6 = 6u * 3600u * 1000u;
    uint32_t now = 1000;
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", now));
    fwAttemptFailed(t, "v4.40b", now);
    TEST_ASSERT_EQUAL_UINT8(1, t.attempts);
    TEST_ASSERT_EQUAL_STRING("v4.40b", t.attemptTag);

    // 6 h spacing
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now + 1));
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now + H6 - 1u));
    now += H6;
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", now));
    fwAttemptFailed(t, "v4.40b", now);
    TEST_ASSERT_EQUAL_UINT8(2, t.attempts);

    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now + H6 - 1u));
    now += H6;
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", now));
    fwAttemptFailed(t, "v4.40b", now);
    TEST_ASSERT_EQUAL_UINT8(3, t.attempts);

    // exhausted: blocked however long we wait
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now + H6));
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now + 10u * H6));
}

static void test_attempts_new_tag_resets(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 1);
    for (int i = 0; i < 3; i++)
        fwAttemptFailed(t, "v4.40b", 1000u + (uint32_t)i * 1000u);
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", 100000));
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.41a", 4000));  // new tag, immediately
    fwAttemptFailed(t, "v4.41a", 4000);
    TEST_ASSERT_EQUAL_UINT8(1, t.attempts);
    TEST_ASSERT_EQUAL_STRING("v4.41a", t.attemptTag);
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.41a", 4001));
    // back to the old tag counts as new again
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", 4001));
}

static void test_attempts_dated_tags_are_distinct(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 1);
    for (int i = 0; i < 3; i++)
        fwAttemptFailed(t, "v4.40a.10.02", 1000u + (uint32_t)i);
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40a.10.02", 1002));
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40a.10.03", 1002));
}

static void test_attempts_long_tag_is_truncated_safely(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 1);
    const char *longTag = "v4.40a.10.02-with-a-very-long-suffix-xx";
    fwAttemptFailed(t, longTag, 1000);
    TEST_ASSERT_EQUAL_UINT32(23, strlen(t.attemptTag));
    TEST_ASSERT_EQUAL_UINT8(1, t.attempts);
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, longTag, 1001));
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, nullptr, 1001));
}

static void test_attempts_no_failure_no_limit(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 1);
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", 0));
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", 0)); // asking does not consume
    TEST_ASSERT_EQUAL_UINT8(0, t.attempts);
}

static void test_layout_inflate_refusal(void)
{
    uint32_t off = 0;
    uint32_t slot = 3324u * KB;
    uint32_t z = 1050u * KB; // stage_off 2240 KB
    TEST_ASSERT_EQUAL_UINT8(FW_OK, fwStageLayout(slot, 1760u * KB, z, 2240u * KB, off));
    TEST_ASSERT_EQUAL_UINT32(2240u * KB, off);
    TEST_ASSERT_EQUAL_UINT8(FW_REF_INFLATE, fwStageLayout(slot, 1760u * KB, z, 2240u * KB + 1u, off));
    TEST_ASSERT_EQUAL_UINT32(2240u * KB, off); // informational
    TEST_ASSERT_EQUAL_UINT8(FW_REF_INFLATE, fwStageLayout(slot, 1760u * KB, z, 0, off));
    TEST_ASSERT_EQUAL_UINT8(FW_REF_INFLATE, fwStageLayout(slot, 1760u * KB, z, 0xFFFFFFFFu, off));
    // a normal 1.8 MB inflated image passes
    TEST_ASSERT_EQUAL_UINT8(FW_OK, fwStageLayout(slot, 1760u * KB, z, 1800u * KB, off));
    // earlier refusals keep their precedence
    TEST_ASSERT_EQUAL_UINT8(FW_REF_SIZE, fwStageLayout(slot, 1760u * KB, 0, 0, off));
    TEST_ASSERT_EQUAL_UINT8(FW_REF_OVERLAP, fwStageLayout(slot, 2300u * KB, z, 0, off));
}

static void test_record_ilen_round_trip_extremes(void)
{
    FwStageRecord r = sampleRecord();
    r.ilen = 0xFFFFFFFFu;
    uint8_t buf[FW_STAGE_ENC_LEN];
    TEST_ASSERT_EQUAL_UINT32(112u, fwRecordEncode(r, buf, sizeof(buf)));
    FwStageRecord o;
    TEST_ASSERT_TRUE(fwRecordDecode(buf, sizeof(buf), o));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, o.ilen);
    // an old 108-byte FWS1 blob is rejected (size and magic)
    TEST_ASSERT_FALSE(fwRecordDecode(buf, 108, o));
    buf[3] = '1';
    TEST_ASSERT_FALSE(fwRecordDecode(buf, sizeof(buf), o));
}

// ---------------------------------------------------------- install guard

static void test_should_install_basic(void)
{
    FwVersion run = V("v4.40a");
    TEST_ASSERT_TRUE(fwShouldInstall(run, V("v4.40b"), "v4.40b", nullptr));
    TEST_ASSERT_TRUE(fwShouldInstall(run, V("v4.40b"), "v4.40b", ""));
    TEST_ASSERT_TRUE(fwShouldInstall(run, V("v4.40b"), "v4.40b", "v4.40a.10.02"));
    // not newer
    TEST_ASSERT_FALSE(fwShouldInstall(run, V("v4.40a"), "v4.40a", nullptr));
    TEST_ASSERT_FALSE(fwShouldInstall(V("v4.40b"), V("v4.40a"), "v4.40a", nullptr));
    // bad tag text
    TEST_ASSERT_FALSE(fwShouldInstall(run, V("v4.40b"), nullptr, nullptr));
    TEST_ASSERT_FALSE(fwShouldInstall(run, V("v4.40b"), "", nullptr));
    FwVersion bad;
    fwParseTag("junk", bad);
    TEST_ASSERT_FALSE(fwShouldInstall(run, bad, "junk", nullptr));
}

static void test_should_install_blocks_already_installed_tag(void)
{
    FwVersion run = V("v4.40a");
    TEST_ASSERT_FALSE(fwShouldInstall(run, V("v4.41a"), "v4.41a", "v4.41a"));
    // dated tag
    TEST_ASSERT_FALSE(fwShouldInstall(run, V("v4.40a.10.02"), "v4.40a.10.02", "v4.40a.10.02"));
    TEST_ASSERT_TRUE(fwShouldInstall(run, V("v4.40a.10.03"), "v4.40a.10.03", "v4.40a.10.02"));
    // a longer tag differing only past the 23 stored chars counts as installed
    TEST_ASSERT_FALSE(fwShouldInstall(run, V("v4.41a"), "v4.41a", "v4.41a"));
}

static void test_should_install_reinstall_loop_untagged_release(void)
{
    // The running image has no MC_BUILD_TAG: SOURCE_VERSION+SUB = "4.40a", undated.
    // The release v4.40a.10.02 was itself built without a tag, so after the
    // update the node still reports "4.40a" and sees the release as newer.
    FwVersion running = V("4.40a");
    FwVersion cand = V("v4.40a.10.02");
    TEST_ASSERT_TRUE(fwCompare(running, cand) > 0); // the trap
    char lastInstalled[24] = "";
    // round 1: install
    TEST_ASSERT_TRUE(fwShouldInstall(running, cand, "v4.40a.10.02", lastInstalled));
    strcpy(lastInstalled, "v4.40a.10.02"); // caller persists after hand-over
    // round 2..n: still "4.40a" undated, same release -> no reinstall
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_TRUE(fwCompare(running, cand) > 0);
        TEST_ASSERT_FALSE(fwShouldInstall(running, cand, "v4.40a.10.02", lastInstalled));
    }
    // a genuinely newer release still installs
    TEST_ASSERT_TRUE(fwShouldInstall(running, V("v4.40b.11.01"), "v4.40b.11.01", lastInstalled));
}

// ------------------------------------------------------------- wrap latch

static void test_latch_check_survives_25_days_without_wifi(void)
{
    FwTimer t;
    uint32_t now = 5000;
    fwTimerInit(t, now, 11);
    FwPolicyIn in = okIn(1);
    in.wifiUp = false;
    const uint32_t HOUR = 3600u * 1000u;
    // ~25 days, one tick per hour, WiFi down the whole time
    for (int h = 0; h < 25 * 24; h++)
    {
        now += HOUR;
        TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, now));
    }
    TEST_ASSERT_TRUE((uint32_t)(now - 5000u) > 0x80000000u); // beyond 2^31 ms
    in.wifiUp = true;
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, now));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, now + 1000u));
}

static void test_latch_check_survives_for_every_blocker(void)
{
    const uint32_t HOUR = 3600u * 1000u;
    for (int blocker = 0; blocker < 4; blocker++)
    {
        FwTimer t;
        uint32_t now = 0xF0000000u; // also wraps while waiting
        fwTimerInit(t, now, 21);
        FwPolicyIn in = okIn(1);
        if (blocker == 0)
            in.mode = 0;
        if (blocker == 1)
            in.wifiUp = false;
        if (blocker == 2)
            in.timeValid = false;
        if (blocker == 3)
            in.phoneConnected = true;
        for (int h = 0; h < 26 * 24; h++)
        {
            now += HOUR;
            TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, now));
        }
        in = okIn(1);
        TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, now));
    }
}

static void test_latch_does_not_pull_check_forward(void)
{
    // before the deadline a blocked tick must not move it
    FwTimer t;
    fwTimerInit(t, 0, 3);
    FwPolicyIn in = okIn(1);
    in.wifiUp = false;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, 60000u));
    TEST_ASSERT_EQUAL_UINT32(FW_FIRST_CHECK_MS, t.nextCheckMs);
    // after a done check the 22..26 h schedule is intact while WiFi is down
    fwCheckDone(t, 1000u);
    uint32_t next = t.nextCheckMs;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, 3600u * 1000u));
    TEST_ASSERT_EQUAL_UINT32(next, t.nextCheckMs);
    in.wifiUp = true;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, next - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, next));
}

static void test_latch_attempt_deadline_survives_25_days(void)
{
    FwTimer t;
    uint32_t now = 100;
    fwTimerInit(t, now, 4);
    fwAttemptFailed(t, "v4.40b", now);
    FwPolicyIn in = okIn(0); // idle, ticks only
    const uint32_t HOUR = 3600u * 1000u;
    for (int h = 0; h < 26 * 24; h++)
    {
        now += HOUR;
        fwTick(t, in, false, now);
        if (h < 5)
            TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now)); // inside the 6 h spacing
    }
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", now));
    TEST_ASSERT_EQUAL_UINT8(1, t.attempts);
}

static void test_latch_attempt_exhausted_stays_blocked(void)
{
    FwTimer t;
    uint32_t now = 100;
    fwTimerInit(t, now, 4);
    for (int i = 0; i < 3; i++)
        fwAttemptFailed(t, "v4.40b", now);
    FwPolicyIn in = okIn(0);
    for (int h = 0; h < 26 * 24; h++)
    {
        now += 3600u * 1000u;
        fwTick(t, in, false, now);
    }
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now));
}

// ------------------------------------------------------------- millis wrap

static void test_wrap_first_check(void)
{
    FwTimer t;
    uint32_t start = 0xFFFFFFFFu - 60000u; // one minute before the wrap
    fwTimerInit(t, start, 3);
    FwPolicyIn in = okIn(1);
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, start));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, 0u));               // wrapped, 1 min in
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, start + 599999u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, start + 600000u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, 600000u));         // past the wrap
}

static void test_wrap_next_check(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 3);
    uint32_t now = 0xFFFFFFFFu - 1000u;
    fwCheckDone(t, now);
    uint32_t d = t.nextCheckMs - now;
    TEST_ASSERT_TRUE(d >= 22u * 3600u * 1000u && d <= 26u * 3600u * 1000u);
    FwPolicyIn in = okIn(1);
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, now + 1000u));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, t.nextCheckMs - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, t.nextCheckMs));
}

static void test_wrap_attempt_spacing(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 3);
    const uint32_t H6 = 6u * 3600u * 1000u;
    uint32_t now = 0xFFFFFFFFu - 3600000u; // 1 h before the wrap
    fwAttemptFailed(t, "v4.40b", now);
    TEST_ASSERT_FALSE(fwAttemptAllowed(t, "v4.40b", now + H6 - 1u));
    TEST_ASSERT_TRUE(fwAttemptAllowed(t, "v4.40b", now + H6)); // after the wrap
    TEST_ASSERT_TRUE(now + H6 < now);                           // really wrapped
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_plain);
    RUN_TEST(test_parse_dated);
    RUN_TEST(test_parse_without_v);
    RUN_TEST(test_parse_date_edges);
    RUN_TEST(test_parse_invalid);
    RUN_TEST(test_compare_version_order);
    RUN_TEST(test_compare_version_beats_date);
    RUN_TEST(test_compare_same_version_date);
    RUN_TEST(test_compare_new_year);
    RUN_TEST(test_compare_half_year_window);
    RUN_TEST(test_compare_antisymmetric_every_day);
    RUN_TEST(test_compare_leap_day_is_feb28);
    RUN_TEST(test_compare_dated_vs_undated);
    RUN_TEST(test_compare_fork_vs_upstream_same_letter);
    RUN_TEST(test_compare_invalid_is_zero);
    RUN_TEST(test_channel_repos);
    RUN_TEST(test_layout_ok_3324kb_slot);
    RUN_TEST(test_layout_exact_alignment);
    RUN_TEST(test_layout_overlap);
    RUN_TEST(test_layout_size_refusals);
    RUN_TEST(test_layout_small);
    RUN_TEST(test_layout_16mb_table);
    RUN_TEST(test_record_magic_value);
    RUN_TEST(test_record_round_trip);
    RUN_TEST(test_record_little_endian_layout);
    RUN_TEST(test_record_encode_is_deterministic);
    RUN_TEST(test_record_small_buffer);
    RUN_TEST(test_record_unterminated_string_on_encode_is_truncated);
    RUN_TEST(test_record_decode_rejects_bad_magic);
    RUN_TEST(test_record_decode_rejects_wrong_size);
    RUN_TEST(test_record_decode_rejects_unterminated_strings);
    RUN_TEST(test_first_check_after_ten_minutes);
    RUN_TEST(test_no_check_without_preconditions);
    RUN_TEST(test_never_with_phone);
    RUN_TEST(test_jitter_bounds_many_seeds);
    RUN_TEST(test_jitter_differs_between_seeds_and_rounds);
    RUN_TEST(test_check_due_at_next_check);
    RUN_TEST(test_handover_window_edges);
    RUN_TEST(test_handover_needs_mode_auto_and_staged);
    RUN_TEST(test_handover_needs_no_wifi);
    RUN_TEST(test_handover_needs_valid_time);
    RUN_TEST(test_handover_battery_floor);
    RUN_TEST(test_handover_beats_due_check);
    RUN_TEST(test_battery_does_not_gate_check);
    RUN_TEST(test_attempts_three_per_tag);
    RUN_TEST(test_attempts_new_tag_resets);
    RUN_TEST(test_attempts_dated_tags_are_distinct);
    RUN_TEST(test_attempts_long_tag_is_truncated_safely);
    RUN_TEST(test_attempts_no_failure_no_limit);
    RUN_TEST(test_wrap_first_check);
    RUN_TEST(test_wrap_next_check);
    RUN_TEST(test_wrap_attempt_spacing);
    RUN_TEST(test_layout_inflate_refusal);
    RUN_TEST(test_record_ilen_round_trip_extremes);
    RUN_TEST(test_should_install_basic);
    RUN_TEST(test_should_install_blocks_already_installed_tag);
    RUN_TEST(test_should_install_reinstall_loop_untagged_release);
    RUN_TEST(test_latch_check_survives_25_days_without_wifi);
    RUN_TEST(test_latch_check_survives_for_every_blocker);
    RUN_TEST(test_latch_does_not_pull_check_forward);
    RUN_TEST(test_latch_attempt_deadline_survives_25_days);
    RUN_TEST(test_latch_attempt_exhausted_stays_blocked);
    return UNITY_END();
}
