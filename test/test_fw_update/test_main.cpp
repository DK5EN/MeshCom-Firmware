// Host test for src/fw_update.h -- pure logic of the firmware auto update (#1187).
//
//   pio test -e native_fw_update
//
// Covers: tag parsing, the version/date comparator (AU-D10), the channel
// repos (AU-D2), the staging layout (AU-D11), the NVS stage record, and the
// check/handover policy with its timer, jitter and retry limits (AU-D1..D5).

#include <unity.h>

#include <stdint.h>
#include <stdio.h>
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
#if defined(MC_AU_DEV_CHANNEL)
    TEST_ASSERT_EQUAL_INT(1, FW_HAS_DEV_CHANNEL);
    TEST_ASSERT_EQUAL_STRING("DK5EN/MeshCom-Firmware", fwChannelRepo(FW_CH_DEV));
#else
    // native_fw_update_prod, the upstream build: no dev channel, a stored dev reads prod
    TEST_ASSERT_EQUAL_INT(0, FW_HAS_DEV_CHANNEL);
    TEST_ASSERT_EQUAL_STRING("icssw-org/MeshCom-Firmware", fwChannelRepo(FW_CH_DEV));
#endif
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
            // D16: a phone that stays connected 30 min past the due time asks for a guarded check
            // (never a plain CHECK); every other blocker stays silent.
            TEST_ASSERT_EQUAL_UINT8(blocker == 3 && h > 0 ? FW_CHECK_GUARDED : FW_NONE, fwTick(t, in, false, now));
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

// #1187: pre-TLS heap gate. The old rule looked at the largest block only.
static bool oldBlockOnlyRule(uint32_t largestBlk) { return largestBlk >= 17000u; }

void test_heap_gate_total_free_classic(void)
{
    // failing T-Beam run (phone on BLE): block rule passes, TLS then failed with -10368
    TEST_ASSERT_TRUE(oldBlockOnlyRule(57332));
    TEST_ASSERT_FALSE(fwNetHeapOk(59832, 57332, 76000u, 17000u));
    // good run (BLE paused)
    TEST_ASSERT_TRUE(fwNetHeapOk(88180, 57332, 76000u, 17000u));
    TEST_ASSERT_TRUE(fwNetHeapOk(76000, 17000, 76000u, 17000u));
    TEST_ASSERT_FALSE(fwNetHeapOk(75999, 57332, 76000u, 17000u));
    TEST_ASSERT_FALSE(fwNetHeapOk(88180, 16999, 76000u, 17000u));
}

void test_heap_gate_internal_tdeck_refuses(void)
{
    // AU-13 field case: 8BIT block 4456436 (PSRAM) passed the old rule, internal numbers must refuse
    TEST_ASSERT_TRUE(oldBlockOnlyRule(4456436));
    TEST_ASSERT_FALSE(fwNetHeapGate(false, 20000, 15000, 0, 0u, 17000u, 24000u));
    // without the allocator a large PSRAM block must not count
    TEST_ASSERT_FALSE(fwNetHeapGate(false, 20000, 15000, 4456436, 0u, 17000u, 24000u));
    // S3 without PSRAM keeps 0/17000 on internal numbers
    TEST_ASSERT_TRUE(fwNetHeapGate(false, 20000, 17000, 0, 0u, 17000u, 24000u));
    // classic without PSRAM keeps 76000/17000
    TEST_ASSERT_FALSE(fwNetHeapGate(false, 75999, 57332, 0, 76000u, 17000u, 24000u));
    TEST_ASSERT_TRUE(fwNetHeapGate(false, 88180, 57332, 0, 76000u, 17000u, 24000u));
}

void test_heap_gate_two_record_buffers(void)
{
    // AU-18: DK5EN-92 free 83816 / blk 32756 passed, the second 16717 B record buffer failed
    TEST_ASSERT_FALSE(fwNetHeapGate(false, 83816, 32756, 0, 76000u, 17000u, 24000u, false));
    TEST_ASSERT_TRUE(fwNetHeapGate(false, 83816, 32756, 0, 76000u, 17000u, 24000u, true));
    // cheap checks fail: refused regardless of the probe
    TEST_ASSERT_FALSE(fwNetHeapGate(false, 75999, 32756, 0, 76000u, 17000u, 24000u, true));
    TEST_ASSERT_FALSE(fwNetHeapGate(false, 83816, 16999, 0, 76000u, 17000u, 24000u, true));
    // PSRAM path: records live in PSRAM, the probe does not matter
    TEST_ASSERT_TRUE(fwNetHeapGate(true, 60000, 0, 17000, 76000u, 17000u, 24000u, false));
    TEST_ASSERT_FALSE(fwNetHeapGate(true, 23999, 0, 17000, 76000u, 17000u, 24000u, true));
    TEST_ASSERT_EQUAL_UINT32(16717u, FWNET_TLS_REC_BUF);
}

void test_heap_gate_psram_tls(void)
{
    TEST_ASSERT_TRUE(fwNetHeapGate(true, 24000, 15000, 4456436, 76000u, 17000u, 24000u));
    TEST_ASSERT_FALSE(fwNetHeapGate(true, 23999, 15000, 4456436, 76000u, 17000u, 24000u));
    TEST_ASSERT_FALSE(fwNetHeapGate(true, 60000, 15000, 16999, 76000u, 17000u, 24000u));
    TEST_ASSERT_TRUE(fwNetHeapGate(true, 60000, 0, 17000, 76000u, 17000u, 24000u));
}

void test_calloc_size_overflow(void)
{
    size_t t = 0;
    TEST_ASSERT_TRUE(fwCallocSizeOk(4, 16384, &t));
    TEST_ASSERT_EQUAL_UINT32(65536u, (uint32_t)t);
    TEST_ASSERT_TRUE(fwCallocSizeOk(0, 16, &t));
    TEST_ASSERT_TRUE(fwCallocSizeOk(16, 0, &t));
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)t);
    TEST_ASSERT_FALSE(fwCallocSizeOk((size_t)-1, 2, &t));
    TEST_ASSERT_FALSE(fwCallocSizeOk((size_t)-1 / 2 + 1, 2, nullptr));
    TEST_ASSERT_TRUE(fwCallocSizeOk((size_t)-1 / 2, 2, nullptr));
}

void test_heap_gate_total_free_disabled(void)
{
    // non-classic: minFree 0 behaves exactly like the old block-only rule
    TEST_ASSERT_TRUE(fwNetHeapOk(20000, 17000, 0u, 17000u));
    TEST_ASSERT_FALSE(fwNetHeapOk(20000, 16999, 0u, 17000u));
}

void test_lowheap_does_not_consume_attempt(void)
{
    TEST_ASSERT_FALSE(fwErrConsumesAttempt("lowheap"));
    TEST_ASSERT_TRUE(fwErrConsumesAttempt("lowheap_ble_off"));   // BLE paused, still too little heap: bounded
    TEST_ASSERT_TRUE(fwErrConsumesAttempt("lowheapx"));          // exact match only
    TEST_ASSERT_TRUE(fwErrConsumesAttempt("lowhea"));
    TEST_ASSERT_TRUE(fwErrConsumesAttempt("tls"));
    TEST_ASSERT_TRUE(fwErrConsumesAttempt("sha"));
    TEST_ASSERT_TRUE(fwErrConsumesAttempt(nullptr));
}

// ---- AU web GUI campaign: modes (D1/D2/D14), skip reasons (D9), guarded check (D16), apply reboot

static void test_effective_mode_maps_auto_to_notify(void)
{
    TEST_ASSERT_EQUAL_INT(0, fwEffectiveMode(0));
    TEST_ASSERT_EQUAL_INT(1, fwEffectiveMode(1));
    TEST_ASSERT_EQUAL_INT(1, fwEffectiveMode(2)); // D14: auto behaves like notify in this build
    TEST_ASSERT_EQUAL_INT(0, AU_AUTO_INSTALL);    // fixed, #error otherwise
}

// D14: with the mode the node actually feeds the policy (fwEffectiveMode), a staged image inside the
// window with every gate open never yields FW_HANDOVER, at any time of the day and any clock value.
static void test_auto_never_hands_over_through_effective_mode(void)
{
    for (uint32_t k = 0; k < 3; k++)
    {
        FwTimer t;
        fwTimerInit(t, 0, 5 + k);
        fwCheckDone(t, 0);
        FwPolicyIn in = okIn((uint8_t)fwEffectiveMode(2));
        in.onBattery = false;
        for (uint16_t m = 0; m < 1440; m += 7)
        {
            in.localMinuteOfDay = m;
            TEST_ASSERT_TRUE(fwTick(t, in, true, 1000u + k) != FW_HANDOVER);
        }
        in.localMinuteOfDay = 4 * 60; // inside the 03:00-05:00 window
        TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, true, 1000));
    }
}

static void test_auto_skip_reason_order_and_none(void)
{
    // (autoCompiled, timeValid, phone, battOk, backedOff, attemptAllowed, staged)
    TEST_ASSERT_EQUAL_STRING("blocked", fwAutoSkipReason(false, true, false, true, false, true, false));
    TEST_ASSERT_EQUAL_STRING("blocked", fwAutoSkipReason(false, false, true, false, true, false, true)); // blocked wins
    TEST_ASSERT_NULL(fwAutoSkipReason(true, true, false, true, false, true, false));
    TEST_ASSERT_EQUAL_STRING("clock", fwAutoSkipReason(true, false, true, false, true, false, true));
    TEST_ASSERT_EQUAL_STRING("phone", fwAutoSkipReason(true, true, true, false, true, false, true));
    TEST_ASSERT_EQUAL_STRING("battery", fwAutoSkipReason(true, true, false, false, true, false, true));
    TEST_ASSERT_EQUAL_STRING("backoff", fwAutoSkipReason(true, true, false, true, true, false, true));
    TEST_ASSERT_EQUAL_STRING("attempts", fwAutoSkipReason(true, true, false, true, false, false, true));
    TEST_ASSERT_EQUAL_STRING("staged", fwAutoSkipReason(true, true, false, true, false, true, true));
}

static void test_lowheap_phone_does_not_consume_attempt(void)
{
    TEST_ASSERT_FALSE(fwErrConsumesAttempt("lowheap_phone"));
    TEST_ASSERT_TRUE(fwErrConsumesAttempt("lowheap_phonex")); // exact match only
    TEST_ASSERT_TRUE(fwErrConsumesAttempt("lowheap_ble_off"));
}

static void test_guarded_check_after_30min_with_phone(void)
{
    FwTimer t;
    fwTimerInit(t, 0, 7);
    const uint32_t due = FW_FIRST_CHECK_MS;
    const uint32_t G = FW_PHONE_GUARD_MS;
    TEST_ASSERT_EQUAL_UINT32(30u * 60u * 1000u, G);
    FwPolicyIn in = okIn(1);
    in.phoneConnected = true;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due + G - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK_GUARDED, fwTick(t, in, false, due + G));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK_GUARDED, fwTick(t, in, false, due + G + 5000u)); // until the check starts
    // the check started: schedule moves on, the guard state is gone
    fwCheckDone(t, due + G + 5000u);
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due + G + 6000u));
    // next period: the 30 minutes count again from the new due time
    const uint32_t due2 = t.nextCheckMs;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due2));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due2 + G - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK_GUARDED, fwTick(t, in, false, due2 + G));
}

static void test_guarded_check_phone_drop_restarts_and_other_blockers_stay_blocked(void)
{
    const uint32_t G = FW_PHONE_GUARD_MS;
    FwTimer t;
    fwTimerInit(t, 0, 7);
    const uint32_t due = FW_FIRST_CHECK_MS;
    FwPolicyIn in = okIn(1);
    in.phoneConnected = true;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due));
    // phone leaves for one tick: plain CHECK; the guard timer restarts
    in.phoneConnected = false;
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK, fwTick(t, in, false, due + G - 1u));
    in.phoneConnected = true;
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due + G));
    TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(t, in, false, due + 2u * G - 1u));
    TEST_ASSERT_EQUAL_UINT8(FW_CHECK_GUARDED, fwTick(t, in, false, due + 2u * G));
    // no WiFi, no clock, mode off: never guarded
    for (int b = 0; b < 3; b++)
    {
        FwTimer u;
        fwTimerInit(u, 0, 9);
        FwPolicyIn x = okIn(1);
        x.phoneConnected = true;
        if (b == 0)
            x.wifiUp = false;
        if (b == 1)
            x.timeValid = false;
        if (b == 2)
            x.mode = 0;
        for (uint32_t n = 0; n < 6; n++)
            TEST_ASSERT_EQUAL_UINT8(FW_NONE, fwTick(u, x, false, due + n * G));
    }
}

static void test_apply_reboot_due_delay(void)
{
    TEST_ASSERT_FALSE(fwApplyRebootDue(false, 1000, 99999));
    TEST_ASSERT_FALSE(fwApplyRebootDue(true, 1000, 1000));
    TEST_ASSERT_FALSE(fwApplyRebootDue(true, 1000, 1000 + FW_APPLY_REBOOT_DELAY_MS - 1u));
    TEST_ASSERT_TRUE(fwApplyRebootDue(true, 1000, 1000 + FW_APPLY_REBOOT_DELAY_MS));
    TEST_ASSERT_TRUE(fwApplyRebootDue(true, 0xFFFFFF00u, 0xFFFFFF00u + FW_APPLY_REBOOT_DELAY_MS)); // wrap
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(500u, FW_APPLY_REBOOT_DELAY_MS);
}

static void test_go_handover_ready(void)
{
    // remote go (RM1 "upd go"): no update window, no mode requirement; phone and battery still count
    FwPolicyIn in = okIn(0); // mode off must not matter
    in.localMinuteOfDay = 12 * 60; // outside the window
    TEST_ASSERT_TRUE(fwGoHandoverReady(in));                    // mains
    in.phoneConnected = true;
    TEST_ASSERT_FALSE(fwGoHandoverReady(in));                   // phone
    in.phoneConnected = false;
    in.onBattery = true;
    in.battMv = FW_BATT_FLOOR_MV - 1;
    TEST_ASSERT_FALSE(fwGoHandoverReady(in));                   // battery below floor
    in.battMv = FW_BATT_FLOOR_MV;
    TEST_ASSERT_TRUE(fwGoHandoverReady(in));                    // battery at floor
    in.battMv = 4100;
    TEST_ASSERT_TRUE(fwGoHandoverReady(in));
    in.onBattery = false;
    in.battMv = 0;
    TEST_ASSERT_TRUE(fwGoHandoverReady(in));                    // mains reads 0 mV
}

// ---- remote go: request decision (rm_au_esp32.cpp rmAuGo) ----

static FwGoReq goReq(const char *tag)
{
    // a newer, installable release "v4.40a.10.20" is available, nothing staged, all gates open
    FwGoReq r;
    memset(&r, 0, sizeof(r));
    r.stagedTag = "";
    r.availTag = "v4.40a.10.20";
    r.availNewer = true;
    r.installable = true;
    r.safebootCapable = true;
    r.timeValid = true;
    r.battOk = true;
    r.wifiUp = true;
    r.tag = tag;
    return r;
}

static void test_go_decide_started(void)
{
    FwGoReq r = goReq("v4.40a.10.20");
    TEST_ASSERT_EQUAL_INT(FWGO_STARTED, fwGoDecide(r));
}

static void test_go_decide_busy_wins_over_everything(void)
{
    FwGoReq r = goReq("v4.99z"); // stale as well
    r.busy = true;
    r.wifiUp = false;
    TEST_ASSERT_EQUAL_INT(FWGO_BUSY, fwGoDecide(r));
}

static void test_go_decide_staged_tag_wins(void)
{
    // the staged tag is applied even if avail says "no newer", no asset, no wifi, no clock, a phone
    FwGoReq r = goReq("v4.40a.10.19");
    r.staged = true;
    r.stagedTag = "v4.40a.10.19";
    r.availNewer = false;
    r.installable = false;
    r.wifiUp = false;
    r.timeValid = false;
    r.phoneConnected = true;
    TEST_ASSERT_EQUAL_INT(FWGO_APPLYING, fwGoDecide(r));
    r.safebootCapable = false;
    TEST_ASSERT_EQUAL_INT(FWGO_SAFEBOOT, fwGoDecide(r));
    r.safebootCapable = true;
    r.handoverAttempted = true; // R3: a handover for this tag was already tried this boot
    TEST_ASSERT_EQUAL_INT(FWGO_SAFEBOOT, fwGoDecide(r));
}

static void test_go_decide_staged_flag_without_tag_is_not_a_match(void)
{
    FwGoReq r = goReq("v4.40a.10.19");
    r.staged = true;
    r.stagedTag = ""; // flag set but no tag: nothing to apply
    TEST_ASSERT_EQUAL_INT(FWGO_STALE, fwGoDecide(r));
    r.staged = false;
    r.stagedTag = "v4.40a.10.19"; // tag text without the valid flag
    TEST_ASSERT_EQUAL_INT(FWGO_STALE, fwGoDecide(r));
}

static void test_go_decide_stale(void)
{
    FwGoReq r = goReq("v4.40a.10.19"); // not the available tag
    TEST_ASSERT_EQUAL_INT(FWGO_STALE, fwGoDecide(r));
    r = goReq(nullptr);
    TEST_ASSERT_EQUAL_INT(FWGO_STALE, fwGoDecide(r));
    r = goReq("");
    r.availTag = ""; // no check yet: empty avail never matches, not even an empty tag
    TEST_ASSERT_EQUAL_INT(FWGO_STALE, fwGoDecide(r));
    r = goReq("v4.40a.10.20");
    r.availTag = "";
    TEST_ASSERT_EQUAL_INT(FWGO_STALE, fwGoDecide(r));
}

static void test_go_decide_current_noasset_safeboot_order(void)
{
    FwGoReq r = goReq("v4.40a.10.20");
    r.availNewer = false;
    r.installable = false;
    r.safebootCapable = false;
    TEST_ASSERT_EQUAL_INT(FWGO_CURRENT, fwGoDecide(r)); // CURRENT before NOASSET
    r.availNewer = true;
    TEST_ASSERT_EQUAL_INT(FWGO_NOASSET, fwGoDecide(r)); // NOASSET before SAFEBOOT
    r.installable = true;
    r.wifiUp = false;
    r.timeValid = false;
    TEST_ASSERT_EQUAL_INT(FWGO_SAFEBOOT, fwGoDecide(r)); // SAFEBOOT before NOTREADY / NOWIFI
    r.safebootCapable = true;
    r.handoverAttempted = true;
    TEST_ASSERT_EQUAL_INT(FWGO_SAFEBOOT, fwGoDecide(r)); // R3: attempted this boot
}

static void test_safeboot_why_dio_wins_over_old(void)
{
    // AU-16: a bootloader in DIO mode cannot run the IDF 5 Safeboot at all; it outranks "old"
    TEST_ASSERT_EQUAL_STRING("ok", fwSafebootWhy(true, false, true));
    TEST_ASSERT_EQUAL_STRING("old", fwSafebootWhy(false, false, true));
    TEST_ASSERT_EQUAL_STRING("old", fwSafebootWhy(true, true, true));
    TEST_ASSERT_EQUAL_STRING("dio", fwSafebootWhy(true, false, false));
    TEST_ASSERT_EQUAL_STRING("dio", fwSafebootWhy(false, false, false));
    TEST_ASSERT_EQUAL_STRING("dio", fwSafebootWhy(true, true, false));
}

static void test_check_age_min(void)
{
    // the clock domain of time(): an epoch once NTP/GPS set the system clock, seconds since boot before
    TEST_ASSERT_EQUAL_INT32(-1, fwCheckAgeMin(0u, 5000));                     // no check this boot
    TEST_ASSERT_EQUAL_INT32(2, fwCheckAgeMin(100u, 100 + 150));               // unset clock: uptime seconds
    TEST_ASSERT_EQUAL_INT32(37, fwCheckAgeMin(1792260000u, 1792260000 + 37 * 60 + 59)); // set clock
    TEST_ASSERT_EQUAL_INT32(-1, fwCheckAgeMin(100u, 1792260000));            // clock set after the check
    TEST_ASSERT_EQUAL_INT32(-1, fwCheckAgeMin(1792260000u, 1792250000));     // clock went back
}

static void test_go_decide_notready_low_battery(void)
{
    FwGoReq r = goReq("v4.40a.10.20");
    r.battOk = false; // AU-D17: the auto path starts no download below the battery floor either
    TEST_ASSERT_EQUAL_INT(FWGO_NOTREADY, fwGoDecide(r));
    r.wifiUp = false;
    TEST_ASSERT_EQUAL_INT(FWGO_NOTREADY, fwGoDecide(r)); // before NOWIFI
    r.staged = true;
    r.stagedTag = "v4.40a.10.20";
    TEST_ASSERT_EQUAL_INT(FWGO_APPLYING, fwGoDecide(r)); // no download: the tick waits for the battery
}

static void test_go_decide_notready_before_nowifi(void)
{
    FwGoReq r = goReq("v4.40a.10.20");
    r.timeValid = false; // R5: no valid clock for TLS
    TEST_ASSERT_EQUAL_INT(FWGO_NOTREADY, fwGoDecide(r));
    r.wifiUp = false;
    TEST_ASSERT_EQUAL_INT(FWGO_NOTREADY, fwGoDecide(r)); // before NOWIFI
    r.timeValid = true;
    r.phoneConnected = true; // R5: a phone is connected
    TEST_ASSERT_EQUAL_INT(FWGO_NOTREADY, fwGoDecide(r));
    r.phoneConnected = false;
    TEST_ASSERT_EQUAL_INT(FWGO_NOWIFI, fwGoDecide(r));
    r.wifiUp = true;
    TEST_ASSERT_EQUAL_INT(FWGO_STARTED, fwGoDecide(r));
}

// ---- remote go: tick step (auTick) ----

static FwGoTickIn goTick(void)
{
    // armed go, tag staged, 8 s passed, phone off, mains, Safeboot capable, record matches
    FwGoTickIn t;
    memset(&t, 0, sizeof(t));
    t.stagedMatch = true;
    t.delayPassed = true;
    t.ready = true;
    t.safebootCapable = true;
    t.recordState = FWGO_REC_OK;
    return t;
}

static void test_go_tick_handover_when_everything_passes(void)
{
    FwGoTickIn t = goTick();
    TEST_ASSERT_EQUAL_INT(FWGOS_HANDOVER, fwGoTickStep(t));
    t.recordState = FWGO_REC_UNCHECKED; // the record is read last, only when all gates passed
    TEST_ASSERT_EQUAL_INT(FWGOS_NEED_RECORD, fwGoTickStep(t));
}

static void test_go_tick_busy_waits_for_download(void)
{
    FwGoTickIn t = goTick();
    t.busy = true;
    t.stagedMatch = false;
    TEST_ASSERT_EQUAL_INT(FWGOS_WAIT_DOWNLOAD, fwGoTickStep(t));
}

static void test_go_tick_download_failure_disarms(void)
{
    FwGoTickIn t = goTick();
    t.stagedMatch = false;
    t.downloadFailed = true;
    TEST_ASSERT_EQUAL_INT(FWGOS_FAIL_DOWNLOAD, fwGoTickStep(t));
    t.downloadFailed = true;
    t.stagedMatch = true; // a stale failure does not matter once the tag is staged
    TEST_ASSERT_EQUAL_INT(FWGOS_HANDOVER, fwGoTickStep(t));
}

static void test_go_tick_no_stage_disarms_instead_of_waiting_forever(void)
{
    // R2: not busy, no staged tag that matches: never wait, disarm
    FwGoTickIn t = goTick();
    t.stagedMatch = false;
    TEST_ASSERT_EQUAL_INT(FWGOS_FAIL_NOSTAGE, fwGoTickStep(t));
    // the loaded record carries another tag than the go
    t = goTick();
    t.recordState = FWGO_REC_TAG_DIFFERS;
    TEST_ASSERT_EQUAL_INT(FWGOS_FAIL_NOSTAGE, fwGoTickStep(t));
    t.recordState = FWGO_REC_MISSING;
    TEST_ASSERT_EQUAL_INT(FWGOS_FAIL_NORECORD, fwGoTickStep(t));
}

static void test_go_tick_attempted_disarms_without_go(void)
{
    // R3: the handover for this tag was already tried this boot: no "go", no silent no-op
    FwGoTickIn t = goTick();
    t.attempted = true;
    TEST_ASSERT_EQUAL_INT(FWGOS_FAIL_ATTEMPTED, fwGoTickStep(t));
    t.recordState = FWGO_REC_UNCHECKED;
    t.delayPassed = false; // fails at once, it does not wait for the reply delay
    TEST_ASSERT_EQUAL_INT(FWGOS_FAIL_ATTEMPTED, fwGoTickStep(t));
}

static void test_go_tick_waits_8s_after_the_reply(void)
{
    FwGoTickIn t = goTick();
    t.delayPassed = false;
    t.recordState = FWGO_REC_UNCHECKED;
    TEST_ASSERT_EQUAL_INT(FWGOS_WAIT_REPLY, fwGoTickStep(t));
    t.phoneConnected = true; // the reply delay is reported first
    t.ready = false;
    TEST_ASSERT_EQUAL_INT(FWGOS_WAIT_REPLY, fwGoTickStep(t));
}

static void test_go_tick_phone_and_battery_wait_reasons(void)
{
    FwGoTickIn t = goTick();
    t.recordState = FWGO_REC_UNCHECKED;
    t.ready = false;
    t.phoneConnected = true;
    TEST_ASSERT_EQUAL_INT(FWGOS_WAIT_PHONE, fwGoTickStep(t));
    t.phoneConnected = false;
    TEST_ASSERT_EQUAL_INT(FWGOS_WAIT_BATTERY, fwGoTickStep(t));
}

static void test_go_tick_old_safeboot_disarms(void)
{
    FwGoTickIn t = goTick();
    t.recordState = FWGO_REC_UNCHECKED;
    t.safebootCapable = false;
    TEST_ASSERT_EQUAL_INT(FWGOS_FAIL_SAFEBOOT, fwGoTickStep(t));
    t.ready = false; // not ready is reported before the Safeboot check
    TEST_ASSERT_EQUAL_INT(FWGOS_WAIT_BATTERY, fwGoTickStep(t));
}

// W1 rework items 2/3/6: pure helpers behind the progress percentage, the /austatus uptime key and the
// busy re-check right before the apply reboot.
static void test_progress_pct_uses_its_own_length(void)
{
    TEST_ASSERT_EQUAL_INT(0, fwProgressPct(0, 0));      // no length yet: never divide
    TEST_ASSERT_EQUAL_INT(0, fwProgressPct(500, 0));
    TEST_ASSERT_EQUAL_INT(42, fwProgressPct(420, 1000));
    TEST_ASSERT_EQUAL_INT(100, fwProgressPct(1000, 1000));
    TEST_ASSERT_EQUAL_INT(100, fwProgressPct(2000, 1000)); // clamped
    TEST_ASSERT_EQUAL_INT(99, fwProgressPct(0xFFFFFFFFu - 1u, 0xFFFFFFFFu) );  // 64-bit math, no overflow
}

static void test_json_append_up(void)
{
    char b[64];
    snprintf(b, sizeof(b), "%s", "{\"st\":\"idle\"}");
    size_t n = fwJsonAppendUp(b, sizeof(b), strlen(b), 4711u);
    TEST_ASSERT_EQUAL_STRING("{\"st\":\"idle\",\"up\":4711}", b);
    TEST_ASSERT_EQUAL_UINT32(strlen(b), n);
    snprintf(b, sizeof(b), "%s", "{}");
    n = fwJsonAppendUp(b, sizeof(b), 2, 0u);
    TEST_ASSERT_EQUAL_STRING("{\"up\":0}", b); // empty object: no leading comma
    // no room: unchanged, still valid
    char t[16];
    snprintf(t, sizeof(t), "%s", "{\"st\":\"idle\"}");
    n = fwJsonAppendUp(t, sizeof(t), strlen(t), 4711u);
    TEST_ASSERT_EQUAL_STRING("{\"st\":\"idle\"}", t);
    TEST_ASSERT_EQUAL_UINT32(strlen(t), n);
    // not an object / empty: untouched
    char e[16] = "";
    TEST_ASSERT_EQUAL_UINT32(0, fwJsonAppendUp(e, sizeof(e), 0, 1u));
    snprintf(e, sizeof(e), "%s", "abc");
    TEST_ASSERT_EQUAL_UINT32(3, fwJsonAppendUp(e, sizeof(e), 3, 1u));
    TEST_ASSERT_EQUAL_STRING("abc", e);
    snprintf(b, sizeof(b), "%s", "{\"a\":1}");
    fwJsonAppendUp(b, sizeof(b), strlen(b), 4294967295u);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"up\":4294967295}", b);
}

// /austatus "img": first 8 hex of the running image's ELF SHA-256. The page tells a Safeboot fallback (same image,
// run stays "local", uptime drops) from a new untagged image only by it.
static void test_json_append_img(void)
{
    const uint8_t sha[32] = {0xa1, 0xb2, 0x0c, 0xd4, 0xff, 0xee};
    char b[64];
    snprintf(b, sizeof(b), "%s", "{\"st\":\"idle\",\"up\":5}");
    size_t n = fwJsonAppendImg(b, sizeof(b), strlen(b), sha, sizeof(sha));
    TEST_ASSERT_EQUAL_STRING("{\"st\":\"idle\",\"up\":5,\"img\":\"a1b20cd4\"}", b);
    TEST_ASSERT_EQUAL_UINT32(strlen(b), n);
    snprintf(b, sizeof(b), "%s", "{}");
    n = fwJsonAppendImg(b, sizeof(b), 2, sha, 4);
    TEST_ASSERT_EQUAL_STRING("{\"img\":\"a1b20cd4\"}", b); // empty object: no leading comma
    TEST_ASSERT_EQUAL_UINT32(strlen(b), n);
    // no id (no description, short or blank hash): key omitted, body untouched
    const uint8_t zero[32] = {0};
    snprintf(b, sizeof(b), "%s", "{\"a\":1}");
    TEST_ASSERT_EQUAL_UINT32(7, fwJsonAppendImg(b, sizeof(b), 7, NULL, 32));
    TEST_ASSERT_EQUAL_UINT32(7, fwJsonAppendImg(b, sizeof(b), 7, sha, 3));
    TEST_ASSERT_EQUAL_UINT32(7, fwJsonAppendImg(b, sizeof(b), 7, zero, sizeof(zero)));
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", b);
    // no room: unchanged, still valid (exactly fitting works)
    char t[25];
    snprintf(t, sizeof(t), "%s", "{\"a\":1}");
    TEST_ASSERT_EQUAL_UINT32(7, fwJsonAppendImg(t, 24, 7, sha, 4));
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", t);
    TEST_ASSERT_EQUAL_UINT32(24, fwJsonAppendImg(t, 25, 7, sha, 4));
    TEST_ASSERT_EQUAL_STRING("{\"a\":1,\"img\":\"a1b20cd4\"}", t);
    // not an object: untouched
    char e[16] = "abc";
    TEST_ASSERT_EQUAL_UINT32(3, fwJsonAppendImg(e, sizeof(e), 3, sha, 4));
    TEST_ASSERT_EQUAL_UINT32(0, fwJsonAppendImg(NULL, 16, 0, sha, 4));
}

static void test_apply_reboot_aborts_when_a_job_is_busy(void)
{
    TEST_ASSERT_EQUAL_INT(FWAPPLY_WAIT, fwApplyRebootAct(false, false));
    TEST_ASSERT_EQUAL_INT(FWAPPLY_WAIT, fwApplyRebootAct(false, true));
    TEST_ASSERT_EQUAL_INT(FWAPPLY_GO, fwApplyRebootAct(true, false));
    TEST_ASSERT_EQUAL_INT(FWAPPLY_ABORT_BUSY, fwApplyRebootAct(true, true));
}

// ---- W4a: BLE guard state machine (spec 7), pure decisions ----
static void test_bleguard_idle_never_acts(void)
{
    TEST_ASSERT_EQUAL(FWG_HOLD, fwGuardStep(false, false, true, true, true, true, true, 1000000u, 0u));
    TEST_ASSERT_EQUAL(FWG_HOLD, fwGuardStep(false, false, false, false, false, false, false, 99999999u, 0u));
}

static void test_bleguard_busy_holds_even_past_the_cap(void)
{
    TEST_ASSERT_EQUAL(FWG_HOLD, fwGuardStep(true, true, false, false, false, false, false, 100u, 0u));
    TEST_ASSERT_EQUAL(FWG_HOLD, fwGuardStep(true, true, false, false, false, false, false, FW_BLEGUARD_CAP_MS + 5u, 0u));
    // a stale "ended" flag does not release a guard while a job runs
    TEST_ASSERT_EQUAL(FWG_HOLD, fwGuardStep(true, true, true, false, false, false, false, 100u, 0u));
}

static void test_bleguard_releases_on_the_last_edge(void)
{
    TEST_ASSERT_EQUAL(FWG_REL_DONE, fwGuardStep(true, false, true, false, false, false, false, 5000u, 1000u));
    TEST_ASSERT_EQUAL(FWG_REL_FAIL, fwGuardStep(true, false, true, true, false, false, false, 5000u, 1000u));
    TEST_ASSERT_EQUAL(FWG_REL_REFUSED, fwGuardStep(true, false, true, true, true, false, false, 5000u, 1000u));
}

static void test_bleguard_chain_only_when_requested_and_check_result_installable(void)
{
    // chain requested + newer installable result: the loop starts the DOWNLOAD under the same guard
    TEST_ASSERT_EQUAL(FWG_CHAIN_DOWNLOAD, fwGuardStep(true, false, true, false, false, true, true, 5000u, 1000u));
    // chain not requested: done, even when a newer release is there
    TEST_ASSERT_EQUAL(FWG_REL_DONE, fwGuardStep(true, false, true, false, false, false, true, 5000u, 1000u));
    // chain requested, result not newer / not installable / already staged: done
    TEST_ASSERT_EQUAL(FWG_REL_DONE, fwGuardStep(true, false, true, false, false, true, false, 5000u, 1000u));
    // a failed CHECK never chains
    TEST_ASSERT_EQUAL(FWG_REL_FAIL, fwGuardStep(true, false, true, true, false, true, true, 5000u, 1000u));
    TEST_ASSERT_EQUAL(FWG_REL_REFUSED, fwGuardStep(true, false, true, true, true, true, true, 5000u, 1000u));
}

static void test_bleguard_chain_ok_needs_newer_installable_unstaged(void)
{
    TEST_ASSERT_TRUE(fwGuardChainOk(true, true, true, false));
    TEST_ASSERT_FALSE(fwGuardChainOk(false, true, true, false));
    TEST_ASSERT_FALSE(fwGuardChainOk(true, false, true, false));
    TEST_ASSERT_FALSE(fwGuardChainOk(true, true, false, false)); // no tag
    TEST_ASSERT_FALSE(fwGuardChainOk(true, true, true, true));   // already staged
}

static void test_bleguard_watchdog_cap_and_wrap(void)
{
    // no job, nothing ended: hold below the cap, watchdog at the cap
    TEST_ASSERT_EQUAL(FWG_HOLD, fwGuardStep(true, false, false, false, false, false, false, FW_BLEGUARD_CAP_MS - 1u, 0u));
    TEST_ASSERT_EQUAL(FWG_REL_WATCHDOG, fwGuardStep(true, false, false, false, false, false, false, FW_BLEGUARD_CAP_MS, 0u));
    TEST_ASSERT_EQUAL_UINT32(40u * 60u * 1000u, FW_BLEGUARD_CAP_MS);
    // millis() wrap: guard set 1000 ms before the wrap
    const uint32_t set = 0xFFFFFFFFu - 1000u;
    TEST_ASSERT_FALSE(fwGuardCapReached(set + 60000u, set));
    TEST_ASSERT_TRUE(fwGuardCapReached(set + FW_BLEGUARD_CAP_MS, set));
    TEST_ASSERT_TRUE(fwGuardCapReached(set + FW_BLEGUARD_CAP_MS + 7u, set));
    TEST_ASSERT_EQUAL(FWG_REL_WATCHDOG, fwGuardStep(true, false, false, false, false, false, false, set + FW_BLEGUARD_CAP_MS, set));
}

static void test_bleguard_release_words_and_phone_rule(void)
{
    TEST_ASSERT_EQUAL_STRING("done", fwGuardReleaseWord(FWG_REL_DONE));
    TEST_ASSERT_EQUAL_STRING("fail", fwGuardReleaseWord(FWG_REL_FAIL));
    TEST_ASSERT_EQUAL_STRING("refused", fwGuardReleaseWord(FWG_REL_REFUSED));
    TEST_ASSERT_EQUAL_STRING("watchdog", fwGuardReleaseWord(FWG_REL_WATCHDOG));
    TEST_ASSERT_EQUAL_STRING("", fwGuardReleaseWord(FWG_HOLD));
    TEST_ASSERT_EQUAL_STRING("", fwGuardReleaseWord(FWG_CHAIN_DOWNLOAD));
    TEST_ASSERT_TRUE(fwGuardRefusedErr("lowheap"));
    TEST_ASSERT_TRUE(fwGuardRefusedErr("lowheap_ble_off"));
    TEST_ASSERT_TRUE(fwGuardRefusedErr("lowheap_phone"));
    TEST_ASSERT_FALSE(fwGuardRefusedErr("tls"));
    TEST_ASSERT_FALSE(fwGuardRefusedErr(nullptr));
    // while the guard is set the phone counts as connected for the handover decisions
    TEST_ASSERT_TRUE(fwGuardPhoneConnected(false, true));
    TEST_ASSERT_TRUE(fwGuardPhoneConnected(true, false));
    TEST_ASSERT_FALSE(fwGuardPhoneConnected(false, false));
}

// W4 rework: service shedding (spec 8.1 step 3) and the notify decision of a finished CHECK.
static void test_shed_allowed_not_while_a_phone_holds_ble(void)
{
    // phone kept BLE up: the pause did not happen, shedding the operator's console would not change the verdict
    TEST_ASSERT_FALSE(fwShedAllowed(false, false, true));
    // paused / guarded / no phone (BLE absent or already down)
    TEST_ASSERT_TRUE(fwShedAllowed(true, false, false));
    TEST_ASSERT_TRUE(fwShedAllowed(false, true, true));
    TEST_ASSERT_TRUE(fwShedAllowed(false, false, false));
}

static void test_shed_first_stage_is_kiss_only(void)
{
    TEST_ASSERT_EQUAL_UINT8(FW_SHED_KISS, fwShedFirst(true, FW_SHED_KISS | FW_SHED_CONSOLE));
    TEST_ASSERT_EQUAL_UINT8(0, fwShedFirst(true, FW_SHED_CONSOLE));
    TEST_ASSERT_EQUAL_UINT8(0, fwShedFirst(true, 0));
    TEST_ASSERT_EQUAL_UINT8(0, fwShedFirst(false, FW_SHED_KISS | FW_SHED_CONSOLE));
}

static void test_shed_second_stage_console_only_when_the_estimate_closes_the_gap(void)
{
    // T-Beam W3: 66-70 KB free against 76000 B: a 4 KB estimate does not close a 6-10 KB gap, the session stays
    TEST_ASSERT_EQUAL_UINT8(0, fwShedSecond(true, FW_SHED_CONSOLE, 68000u, 76000u));
    TEST_ASSERT_EQUAL_UINT8(0, fwShedSecond(true, FW_SHED_CONSOLE, 66000u, 76000u));
    // a gap the estimate covers: shed (free + estimate >= need, boundary inclusive)
    TEST_ASSERT_EQUAL_UINT8(FW_SHED_CONSOLE, fwShedSecond(true, FW_SHED_CONSOLE, 76000u - FW_SHED_EST_CONSOLE, 76000u));
    TEST_ASSERT_EQUAL_UINT8(0, fwShedSecond(true, FW_SHED_CONSOLE, 76000u - FW_SHED_EST_CONSOLE - 1u, 76000u));
    // no session to drop, or not allowed
    TEST_ASSERT_EQUAL_UINT8(0, fwShedSecond(true, FW_SHED_KISS, 75000u, 76000u));
    TEST_ASSERT_EQUAL_UINT8(0, fwShedSecond(false, FW_SHED_CONSOLE, 75000u, 76000u));
}

static void test_shed_settled_waits_only_for_what_was_shed(void)
{
    TEST_ASSERT_FALSE(fwShedSettled(FW_SHED_CONSOLE, true, false));
    TEST_ASSERT_TRUE(fwShedSettled(FW_SHED_CONSOLE, false, true));  // KISS still up but not requested
    TEST_ASSERT_FALSE(fwShedSettled(FW_SHED_KISS, false, true));
    TEST_ASSERT_TRUE(fwShedSettled(FW_SHED_KISS, true, false));
    TEST_ASSERT_FALSE(fwShedSettled(FW_SHED_KISS | FW_SHED_CONSOLE, false, true));
    TEST_ASSERT_TRUE(fwShedSettled(FW_SHED_KISS | FW_SHED_CONSOLE, false, false));
    TEST_ASSERT_TRUE(fwShedSettled(0, true, true));
    TEST_ASSERT_TRUE(FW_SHED_MIN_MS < FW_SHED_MAX_MS && FW_SHED_POLL_MS <= FW_SHED_MIN_MS);
}

static void test_tls_fail_token_blames_heap_when_allocations_failed(void)
{
    TEST_ASSERT_EQUAL_STRING("lowheap_tls", fwTlsFailToken(5u)); // AU-21 field number
    TEST_ASSERT_EQUAL_STRING("lowheap_tls", fwTlsFailToken(1u));
    // a heap failure inside the handshake uses a download attempt (bounded retries, no 10 min loop on the S3)
    TEST_ASSERT_TRUE(fwErrConsumesAttempt(fwTlsFailToken(5u)));
    TEST_ASSERT_EQUAL_STRING("tls", fwTlsFailToken(0u));
    TEST_ASSERT_TRUE(fwGuardRefusedErr(fwTlsFailToken(5u))); // GUI maps it to state H
    TEST_ASSERT_FALSE(fwGuardRefusedErr(fwTlsFailToken(0u)));
}

static void test_shed_settled_waits_for_the_tft_sprite(void)
{
    TEST_ASSERT_EQUAL_UINT8(4u, FW_SHED_TFT);
    TEST_ASSERT_FALSE(fwShedSettled(FW_SHED_TFT, false, false, true));
    TEST_ASSERT_TRUE(fwShedSettled(FW_SHED_TFT, false, false, false));
    TEST_ASSERT_TRUE(fwShedSettled(FW_SHED_KISS, false, false, true)); // sprite held but not requested
    TEST_ASSERT_FALSE(fwShedSettled(FW_SHED_TFT | FW_SHED_KISS, false, true, false));
}

static void test_tft_sprite_wanted_needs_display_on_and_no_shed(void)
{
    TEST_ASSERT_TRUE(fwTftSpriteWanted(false, false, false));
    TEST_ASSERT_FALSE(fwTftSpriteWanted(true, false, false)); // --display off
    TEST_ASSERT_FALSE(fwTftSpriteWanted(false, true, false)); // shed by the update
    TEST_ASSERT_FALSE(fwTftSpriteWanted(true, true, false));
    // a job is running without the hold (an earlier acquire failed, or between CHECK and a chained DOWNLOAD):
    // no 25.6 KB allocation in the middle of the TLS handshake
    TEST_ASSERT_FALSE(fwTftSpriteWanted(false, false, true));
}

static void test_notify_due_for_a_finished_newer_check_once(void)
{
    TEST_ASSERT_TRUE(fwNotifyDue(true, true, 1, false));
    TEST_ASSERT_FALSE(fwNotifyDue(true, true, 1, true));   // announced already
    TEST_ASSERT_FALSE(fwNotifyDue(true, true, 0, false));  // off
    TEST_ASSERT_FALSE(fwNotifyDue(true, true, 2, false));  // 2 acts as 1 only via fwEffectiveMode()
    TEST_ASSERT_FALSE(fwNotifyDue(true, false, 1, false)); // nothing newer
    TEST_ASSERT_FALSE(fwNotifyDue(false, true, 1, false)); // job not a finished OK check
    TEST_ASSERT_TRUE(fwNotifyDue(true, true, fwEffectiveMode(2), false));
}

void test_chan_decide_verdicts(void)
{
    // same channel: idempotent, even while busy or armed
    TEST_ASSERT_EQUAL_INT(FWCH_SAME, fwChanDecide(0, 0, false, false));
    TEST_ASSERT_EQUAL_INT(FWCH_SAME, fwChanDecide(0, 0, true, true));
    // change while a job runs or a go is armed: BUSY, nothing else
    TEST_ASSERT_EQUAL_INT(FWCH_BUSY, fwChanDecide(1, 0, true, false));
    TEST_ASSERT_EQUAL_INT(FWCH_BUSY, fwChanDecide(1, 0, false, true));
#if FW_HAS_DEV_CHANNEL
    TEST_ASSERT_EQUAL_INT(FWCH_SAME, fwChanDecide(1, 1, true, true));
    TEST_ASSERT_EQUAL_INT(FWCH_SAME, fwChanDecide(1, 5, false, false)); // 5 normalises to 1
    TEST_ASSERT_EQUAL_INT(FWCH_CHANGE, fwChanDecide(0, 1, false, false));
    TEST_ASSERT_EQUAL_INT(FWCH_CHANGE, fwChanDecide(1, 0, false, false));
    TEST_ASSERT_EQUAL_INT(FWCH_BUSY, fwChanDecide(0, 1, true, false));
    TEST_ASSERT_EQUAL_INT(FWCH_BUSY, fwChanDecide(0, 1, false, true));
#else
    // prod-only build: dev is never available, busy or not; cur=1 (stale NVS) -> prod is a change
    TEST_ASSERT_EQUAL_INT(FWCH_NODEV, fwChanDecide(0, 1, false, false));
    TEST_ASSERT_EQUAL_INT(FWCH_NODEV, fwChanDecide(0, 1, true, true));
    TEST_ASSERT_EQUAL_INT(FWCH_NODEV, fwChanDecide(1, 1, false, false));
    TEST_ASSERT_EQUAL_INT(FWCH_CHANGE, fwChanDecide(1, 0, false, false));
#endif
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_tls_fail_token_blames_heap_when_allocations_failed);
    RUN_TEST(test_shed_settled_waits_for_the_tft_sprite);
    RUN_TEST(test_tft_sprite_wanted_needs_display_on_and_no_shed);
    RUN_TEST(test_chan_decide_verdicts);
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
    RUN_TEST(test_go_handover_ready);
    RUN_TEST(test_latch_does_not_pull_check_forward);
    RUN_TEST(test_latch_attempt_deadline_survives_25_days);
    RUN_TEST(test_latch_attempt_exhausted_stays_blocked);
    RUN_TEST(test_heap_gate_total_free_classic);
    RUN_TEST(test_heap_gate_total_free_disabled);
    RUN_TEST(test_heap_gate_internal_tdeck_refuses);
    RUN_TEST(test_heap_gate_psram_tls);
    RUN_TEST(test_heap_gate_two_record_buffers);
    RUN_TEST(test_calloc_size_overflow);
    RUN_TEST(test_lowheap_does_not_consume_attempt);
    RUN_TEST(test_effective_mode_maps_auto_to_notify);
    RUN_TEST(test_auto_never_hands_over_through_effective_mode);
    RUN_TEST(test_auto_skip_reason_order_and_none);
    RUN_TEST(test_lowheap_phone_does_not_consume_attempt);
    RUN_TEST(test_guarded_check_after_30min_with_phone);
    RUN_TEST(test_guarded_check_phone_drop_restarts_and_other_blockers_stay_blocked);
    RUN_TEST(test_apply_reboot_due_delay);
    RUN_TEST(test_progress_pct_uses_its_own_length);
    RUN_TEST(test_json_append_up);
    RUN_TEST(test_json_append_img);
    RUN_TEST(test_apply_reboot_aborts_when_a_job_is_busy);
    RUN_TEST(test_go_decide_started);
    RUN_TEST(test_go_decide_busy_wins_over_everything);
    RUN_TEST(test_go_decide_staged_tag_wins);
    RUN_TEST(test_go_decide_staged_flag_without_tag_is_not_a_match);
    RUN_TEST(test_go_decide_stale);
    RUN_TEST(test_go_decide_current_noasset_safeboot_order);
    RUN_TEST(test_check_age_min);
    RUN_TEST(test_safeboot_why_dio_wins_over_old);
    RUN_TEST(test_go_decide_notready_low_battery);
    RUN_TEST(test_go_decide_notready_before_nowifi);
    RUN_TEST(test_go_tick_handover_when_everything_passes);
    RUN_TEST(test_go_tick_busy_waits_for_download);
    RUN_TEST(test_go_tick_download_failure_disarms);
    RUN_TEST(test_go_tick_no_stage_disarms_instead_of_waiting_forever);
    RUN_TEST(test_go_tick_attempted_disarms_without_go);
    RUN_TEST(test_go_tick_waits_8s_after_the_reply);
    RUN_TEST(test_go_tick_phone_and_battery_wait_reasons);
    RUN_TEST(test_go_tick_old_safeboot_disarms);
    RUN_TEST(test_bleguard_idle_never_acts);
    RUN_TEST(test_bleguard_busy_holds_even_past_the_cap);
    RUN_TEST(test_bleguard_releases_on_the_last_edge);
    RUN_TEST(test_bleguard_chain_only_when_requested_and_check_result_installable);
    RUN_TEST(test_bleguard_chain_ok_needs_newer_installable_unstaged);
    RUN_TEST(test_bleguard_watchdog_cap_and_wrap);
    RUN_TEST(test_bleguard_release_words_and_phone_rule);
    RUN_TEST(test_shed_allowed_not_while_a_phone_holds_ble);
    RUN_TEST(test_shed_first_stage_is_kiss_only);
    RUN_TEST(test_shed_second_stage_console_only_when_the_estimate_closes_the_gap);
    RUN_TEST(test_shed_settled_waits_only_for_what_was_shed);
    RUN_TEST(test_notify_due_for_a_finished_newer_check_once);
    return UNITY_END();
}
