// Native-Tests fuer src/cet_age.h (Kampagne TXP, W0).
#include <unity.h>
#include "../support/repo_file.h"
#include <string.h>
#include <stdio.h>
#include "cet_age.h"
#include "cet_filter.h"
#include "time_rank.h"

void setUp(void) {}
void tearDown(void) {}

static const char *P = "{CET}2026-10-24 12:00:00";

static uint16_t buildText(uint8_t *b, const char *src, const char *dst, const char *payload, uint8_t type = 0x3A)
{
    uint16_t n = 0;
    b[n++] = type;
    b[n++] = 1; b[n++] = 2; b[n++] = 3; b[n++] = 4;
    b[n++] = 0x14;
    memcpy(b + n, src, strlen(src)); n += (uint16_t)strlen(src);
    b[n++] = '>';
    memcpy(b + n, dst, strlen(dst)); n += (uint16_t)strlen(dst);
    b[n++] = 0x3A;
    memcpy(b + n, payload, strlen(payload)); n += (uint16_t)strlen(payload);
    b[n++] = 0;
    return n;
}

void test_parse_valid_known_epoch(void)
{
    uint32_t e = 0;
    TEST_ASSERT_TRUE(cetParsePayload(P, strlen(P), &e));
    TEST_ASSERT_EQUAL_UINT32(1792843200UL, e);   // python calendar.timegm
}

void test_parse_leap_day(void)
{
    uint32_t e = 0;
    const char *s = "{CET}2028-02-29 00:00:01";
    TEST_ASSERT_TRUE(cetParsePayload(s, strlen(s), &e));
    TEST_ASSERT_EQUAL_UINT32(1835395201UL, e);
    const char *bad = "{CET}2027-02-29 00:00:01";
    TEST_ASSERT_FALSE(cetParsePayload(bad, strlen(bad), &e));
}

void test_parse_rejects(void)
{
    uint32_t e = 0;
    const char *c[] = {
        "{CET}<2026-10-24 12:00:00", "{CET}2026-10-24 12:00", "{CET}", "",
        "{CET}2026-13-24 12:00:00", "{CET}2026-00-24 12:00:00", "{CET}2026-10-32 12:00:00",
        "{CET}2026-10-00 12:00:00", "{CET}2026-10-24 24:00:00", "{CET}2026-10-24 12:60:00",
        "{CET}2026-10-24 12:00:60", "{CET}2023-12-31 23:59:59", "{CET}2026/10/24 12:00:00",
        "{SET}2026-10-24 12:00:00", "{CET}2026-1x-24 12:00:00"};
    for(size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(cetParsePayload(c[i], strlen(c[i]), &e), c[i]);
    TEST_ASSERT_FALSE(cetParsePayload(NULL, 30, &e));
}

void test_age_sign(void)
{
    TEST_ASSERT_EQUAL_INT32(30, cetAgeS(1000030, 1000000));
    TEST_ASSERT_EQUAL_INT32(-5, cetAgeS(1000000, 1000005));
    TEST_ASSERT_EQUAL_INT32(0, cetAgeS(7, 7));
}

void test_trusted_truth_table(void)
{
    for(int m = 0; m < 8; m++)
        TEST_ASSERT_EQUAL(m != 0, cetClockTrusted(m & 1, m & 2, m & 4));
}

void test_find_in_frame(void)
{
    uint8_t b[300];
    uint32_t e = 0;
    uint16_t n = buildText(b, "OE1XXX-1,DB0AAA", "*", P);
    TEST_ASSERT_TRUE(cetFindInFrame(b, n, &e));
    TEST_ASSERT_EQUAL_UINT32(1792843200UL, e);

    n = buildText(b, "DK5EN-90", "*", "Hallo Welt");
    TEST_ASSERT_FALSE(cetFindInFrame(b, n, &e));

    n = buildText(b, "DK5EN-90", "*", P, 0x21);   // Position, kein Text
    TEST_ASSERT_FALSE(cetFindInFrame(b, n, &e));

    n = buildText(b, "OE1XXX-1", "*", "{CET}<2026-10-24 12:00:00");
    TEST_ASSERT_FALSE(cetFindInFrame(b, n, &e));
    TEST_ASSERT_FALSE(cetFindInFrame(b, 5, &e));
}


// W1a: Wahrheitstabelle cetFrameStale()
static void test_frame_stale_truth_table(void)
{
    uint8_t b[96];
    uint32_t pl;
    TEST_ASSERT_TRUE(cetParsePayload(P, strlen(P), &pl));
    uint16_t n = buildText(b, "OE1XXX-1", "*", P);
    // frisch / Grenze / zu alt
    TEST_ASSERT_FALSE(cetFrameStale(b, n, true, true, pl + 60));
    TEST_ASSERT_FALSE(cetFrameStale(b, n, true, true, pl + 150));
    TEST_ASSERT_TRUE(cetFrameStale(b, n, true, true, pl + 151));
    // Zukunft: ebenfalls falsche Zeit
    TEST_ASSERT_FALSE(cetFrameStale(b, n, true, true, pl - 150));
    TEST_ASSERT_TRUE(cetFrameStale(b, n, true, true, pl - 151));
    TEST_ASSERT_TRUE(cetFrameStale(b, n, true, true, pl - 200));
    // Uhr nicht vertrauenswuerdig / nicht gestellt
    TEST_ASSERT_TRUE(cetFrameStale(b, n, false, true, pl));
    TEST_ASSERT_TRUE(cetFrameStale(b, n, true, false, 0));
    // {CET}< und unparsbar
    n = buildText(b, "OE1XXX-1", "*", "{CET}<2026-10-24 12:00:00");
    TEST_ASSERT_TRUE(cetFrameStale(b, n, true, true, pl));
    n = buildText(b, "OE1XXX-1", "*", "{CET}kaputt");
    TEST_ASSERT_TRUE(cetFrameStale(b, n, true, true, pl));
    // Kein {CET}: behalten, egal welche Uhr
    n = buildText(b, "DK5EN-90", "*", "Hallo");
    TEST_ASSERT_FALSE(cetFrameStale(b, n, false, false, 0));
    n = buildText(b, "DK5EN-90", "*", P, 0x21);   // kein Text-Frame
    TEST_ASSERT_FALSE(cetFrameStale(b, n, false, false, 0));
    TEST_ASSERT_FALSE(cetFrameStale(NULL, 0, false, false, 0));
    // CetClock-Variante
    n = buildText(b, "OE1XXX-1", "*", P);
    CetClock c = {true, true, pl + 10};
    TEST_ASSERT_FALSE(cetFrameStale(b, n, c));
    c.trusted = false;
    TEST_ASSERT_TRUE(cetFrameStale(b, n, c));
}

static bool fileContains(const char *path, const char *needle, long *pos)
{
    FILE *f = repo_fopen(path, "rb");
    if(!f) return false;
    static char buf[1 << 20];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    const char *h = strstr(buf, needle);
    if(!h) return false;
    *pos = (long)(h - buf);
    return true;
}

// CET-AGE-01 R1: rueckwaerts > 5 s binnen 30 min nach letztem Accept -> verwerfen.
void test_step_guard(void)
{
    const long back[] = {-114482 / 1000, -30898 / 1000, -25911 / 1000, -22427 / 1000, -34320 / 1000};
    for(size_t i = 0; i < sizeof(back) / sizeof(back[0]); i++)
        TEST_ASSERT_FALSE(cetStepAllowed(back[i], true, 60UL * 1000UL));
    TEST_ASSERT_TRUE(cetStepAllowed(-5, true, 1000UL));
    TEST_ASSERT_FALSE(cetStepAllowed(-6, true, 1000UL));
    TEST_ASSERT_TRUE(cetStepAllowed(0, true, 1000UL));
    TEST_ASSERT_TRUE(cetStepAllowed(1, true, 1000UL));
    TEST_ASSERT_TRUE(cetStepAllowed(100000, true, 1000UL));
    TEST_ASSERT_TRUE(cetStepAllowed(-114, false, 0UL));
    TEST_ASSERT_FALSE(cetStepAllowed(-114, true, CET_BACKSTEP_WINDOW_MS));
    TEST_ASSERT_TRUE(cetStepAllowed(-114, true, CET_BACKSTEP_WINDOW_MS + 1UL));

    // Anchored on the CALL (not the include comment) and on the gate between call and set (advisor).
    long a = 0, g = 0, b = 0;
    TEST_ASSERT_TRUE_MESSAGE(fileContains("src/loop_functions.cpp", "cetBeaconGate(cetRank, &s_cetFilter", &a), "{CET} set site does not call cetBeaconGate() (-> cetFilterDecide)");
    TEST_ASSERT_TRUE_MESSAGE(fileContains("src/loop_functions.cpp", "if(cetG.action == CETG_FILTER && cetVerdictApplies(cetV))", &g), "{CET} set is not gated by cetVerdictApplies");
    TEST_ASSERT_TRUE(fileContains("src/loop_functions.cpp", "MyClock.setCurrentTime(meshcom_settings.node_utcoff, Year", &b));
    TEST_ASSERT_TRUE_MESSAGE(a < g && g < b, "call -> if(cetVerdictApplies) -> setCurrentTime order broken");
}

// ---- Forward-only {CET} filter (CET-SCATTER-01 Part A) ----
static void refFilter(CetFilter *f) { cetFilterInit(f); TEST_ASSERT_EQUAL(CET_V_FIRST, cetFilterDecide(f, 0, 0)); }

void test_filter_first_any_direction(void)
{
    CetFilter f, g;
    cetFilterInit(&f); cetFilterInit(&g);
    TEST_ASSERT_EQUAL(CET_V_FIRST, cetFilterDecide(&f, -114, 0));
    TEST_ASSERT_EQUAL(CET_V_FIRST, cetFilterDecide(&g, 550, 0));
}

void test_filter_forward(void)
{
    CetFilter f; refFilter(&f);
    TEST_ASSERT_EQUAL(CET_V_FWD, cetFilterDecide(&f, 1, 1000));
    TEST_ASSERT_EQUAL(CET_V_FWD, cetFilterDecide(&f, 25, 2000));
}

void test_filter_band(void)
{
    CetFilter f; refFilter(&f);
    const int32_t d[] = {0, -1, -4, -5};
    for(size_t i = 0; i < 4; i++) TEST_ASSERT_EQUAL(CET_V_BAND, cetFilterDecide(&f, d[i], 1000));
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -6, 1000));
}

void test_filter_quorum(void)
{
    CetFilter f; refFilter(&f);
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, 0));
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, 600000));
    TEST_ASSERT_EQUAL(CET_V_QUORUM, cetFilterDecide(&f, -20, 1200000));
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, 1800000));
}

void test_filter_quorum_needs_consecutive(void)
{
    CetFilter f; refFilter(&f);
    cetFilterDecide(&f, -20, 0); cetFilterDecide(&f, -20, 1000);
    TEST_ASSERT_EQUAL(CET_V_BAND, cetFilterDecide(&f, 0, 2000));
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, 3000));
}

void test_filter_fwd_resets_quorum(void)
{
    CetFilter f; refFilter(&f);
    cetFilterDecide(&f, -20, 0); cetFilterDecide(&f, -20, 1000);
    TEST_ASSERT_EQUAL(CET_V_FWD, cetFilterDecide(&f, 2, 2000));
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, 3000));
}

void test_filter_quorum_window(void)
{
    CetFilter f; refFilter(&f);
    cetFilterDecide(&f, -20, 0); cetFilterDecide(&f, -20, 600000);
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, 1800001UL));
}

void test_filter_millis_wrap(void)
{
    CetFilter f; refFilter(&f);
    const uint32_t t0 = 0xFFFFF000UL;
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, t0));
    TEST_ASSERT_EQUAL(CET_V_BACK, cetFilterDecide(&f, -20, t0 + 600000UL));
    TEST_ASSERT_EQUAL(CET_V_QUORUM, cetFilterDecide(&f, -20, t0 + 1200000UL));
}

struct ReplayOut { CetVerdict v[16]; int applies; int64_t c; };
// Replay harness of the concept (int64 ms). useOld: cetStepAllowed(d, have, 607000) instead of the filter.
static void replay(const long *ages, int n, bool useOld, ReplayOut *o)
{
    CetFilter f; cetFilterInit(&f);
    int64_t c = 0; bool have = false; o->applies = 0;
    for(int k = 0; k < n; k++)
    {
        const int64_t T = 1794779000000LL + (int64_t)k * 607000;
        const int64_t pl = (T - ages[k]) / 1000, now = (T + c) / 1000;
        const int32_t d = (int32_t)(pl - now);
        bool ap;
        if(useOld) { ap = cetStepAllowed(d, have, 607000UL); o->v[k] = ap ? CET_V_FWD : CET_V_BACK; }
        else { o->v[k] = cetFilterDecide(&f, d, (uint32_t)(k * 607000)); ap = cetVerdictApplies(o->v[k]); }
        if(ap) { c = pl * 1000 - T; have = true; o->applies++; }
    }
    o->c = c;
}

void test_filter_replay_tbeam_s3(void)
{
    const long ages[] = {2470, 8180, 2630, 5000, 1290, 3170, 950, 3940, 2210, 5950, 2180};
    const CetVerdict e[] = {CET_V_FIRST, CET_V_BACK, CET_V_BAND, CET_V_BAND, CET_V_FWD, CET_V_BAND, CET_V_FWD, CET_V_BAND, CET_V_BAND, CET_V_BAND, CET_V_BAND};
    ReplayOut o; replay(ages, 11, false, &o);
    for(int i = 0; i < 11; i++) TEST_ASSERT_EQUAL_MESSAGE(e[i], o.v[i], "tbeam verdict");
    TEST_ASSERT_EQUAL(3, o.applies);
    TEST_ASSERT_EQUAL_INT64(-1000, o.c);
}

void test_filter_replay_heltec_s4(void)
{
    const long ages[] = {4710, 1470, 2180, 7630, 5940, 1670};
    const CetVerdict e[] = {CET_V_FIRST, CET_V_FWD, CET_V_BAND, CET_V_BACK, CET_V_BAND, CET_V_BAND};
    ReplayOut o; replay(ages, 6, false, &o);
    for(int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_MESSAGE(e[i], o.v[i], "heltec verdict");
    TEST_ASSERT_EQUAL(2, o.applies);
    TEST_ASSERT_EQUAL_INT64(-2000, o.c);
}

void test_old_rule_oracle_tbeam(void)
{
    const long ages[] = {2470, 8180, 2630, 5000, 1290, 3170, 950, 3940, 2210, 5950, 2180};
    ReplayOut o; replay(ages, 11, true, &o);
    TEST_ASSERT_EQUAL(10, o.applies);
}

// ---- CET-SCATTER-01 Part B: time-source rank with holdover ----
#define H6 (6UL * 3600UL * 1000UL)
#define H1 (1UL * 3600UL * 1000UL)

void test_rank_of(void)
{
    TEST_ASSERT_EQUAL(TR_GPS, timeRankOf("GPS"));
    TEST_ASSERT_EQUAL(TR_NTP, timeRankOf("NTP"));
    TEST_ASSERT_EQUAL(TR_RTC, timeRankOf("RTC"));
    TEST_ASSERT_EQUAL(TR_CET, timeRankOf("CET"));
    TEST_ASSERT_EQUAL(TR_MANUAL, timeRankOf("PHONE"));
    TEST_ASSERT_EQUAL(TR_MANUAL, timeRankOf("CMD"));
    TEST_ASSERT_EQUAL(TR_MANUAL, timeRankOf("SER"));
    TEST_ASSERT_EQUAL(TR_NONE, timeRankOf(NULL));
    TEST_ASSERT_EQUAL(TR_NONE, timeRankOf("OTHER"));
}

void test_rank_holdover_edges(void)
{
    TimeRankState s = {}; uint32_t h = 0;
    // premium sources never expire (operator decision 2026-11-16)
    timeRankNote(&s, TR_NTP, 0);
    TEST_ASSERT_EQUAL(TR_NTP, timeRankBlockingCet(&s, H6 - 1, &h));
    TEST_ASSERT_EQUAL_UINT32(H6 - 1, h);
    TEST_ASSERT_EQUAL(TR_NTP, timeRankBlockingCet(&s, H6, &h));
    TEST_ASSERT_EQUAL(TR_NTP, timeRankBlockingCet(&s, 40UL * H1, &h));
    TimeRankState g = {};
    timeRankNote(&g, TR_GPS, 0);
    TEST_ASSERT_EQUAL(TR_GPS, timeRankBlockingCet(&g, H6 - 1, &h));
    TEST_ASSERT_EQUAL(TR_GPS, timeRankBlockingCet(&g, H6, &h));
    TEST_ASSERT_EQUAL(TR_GPS, timeRankBlockingCet(&g, 40UL * H1, &h));
    TimeRankState m = {};
    timeRankNote(&m, TR_MANUAL, 0);
    TEST_ASSERT_EQUAL(TR_MANUAL, timeRankBlockingCet(&m, H1 - 1, &h));
    TEST_ASSERT_EQUAL(TR_NONE, timeRankBlockingCet(&m, H1, &h));
    TimeRankState r = {};
    timeRankNote(&r, TR_RTC, 0);
    TEST_ASSERT_EQUAL(TR_RTC, timeRankBlockingCet(&r, H6 - 1, &h));
    TEST_ASSERT_EQUAL(TR_NONE, timeRankBlockingCet(&r, H6, &h));
}

void test_rank_highest_wins(void)
{
    TimeRankState s = {}; uint32_t h = 0;
    // NTP outranks GPS, whichever was noted last
    timeRankNote(&s, TR_NTP, 0);
    timeRankNote(&s, TR_GPS, H1);
    TEST_ASSERT_EQUAL(TR_NTP, timeRankBlockingCet(&s, 2 * H1, &h));
    TEST_ASSERT_EQUAL_UINT32(2 * H1, h);
    TimeRankState t = {};
    timeRankNote(&t, TR_GPS, 0);
    timeRankNote(&t, TR_NTP, H1);
    TEST_ASSERT_EQUAL(TR_NTP, timeRankBlockingCet(&t, 2 * H1, &h));
    // phone noted, then GPS: GPS outranks
    TimeRankState p = {};
    timeRankNote(&p, TR_MANUAL, 10); timeRankNote(&p, TR_GPS, 20);
    TEST_ASSERT_EQUAL(TR_GPS, timeRankBlockingCet(&p, 30, &h));
    TEST_ASSERT_TRUE(TR_NTP > TR_GPS && TR_GPS > TR_MANUAL && TR_MANUAL > TR_RTC && TR_RTC > TR_CET && TR_CET > TR_NONE);
}

void test_rank_cet_never_blocks(void)
{
    TimeRankState s = {}; uint32_t h = 0;
    timeRankNote(&s, TR_CET, 0);
    timeRankNote(&s, TR_NONE, 0);
    TEST_ASSERT_EQUAL(TR_NONE, timeRankBlockingCet(&s, 1, &h));
}

void test_rank_wrap(void)
{
    TimeRankState s = {}; uint32_t h = 0;
    timeRankNote(&s, TR_NTP, 0xFFFFF000UL);
    TEST_ASSERT_EQUAL(TR_NTP, timeRankBlockingCet(&s, 0x00001000UL, &h));
    TEST_ASSERT_EQUAL_UINT32(0x2000UL, h);
}

void test_rank_hook_anchor(void)
{
    long pos = 0;
    const char *f = "src/clock_instrument.h";
    TEST_ASSERT_TRUE(fileContains(f, "#define CLK_SRC(s) (clockInstrSrc(s), timeRankNoteSrc(s))", &pos));   // INSTRUMENT
    TEST_ASSERT_TRUE(fileContains(f, "#define CLK_SRC(s) timeRankNoteSrc(s)", &pos));                      // normal
    TEST_ASSERT_TRUE(fileContains("src/loop_functions.cpp", "cetBeaconGate(", &pos));
    TEST_ASSERT_TRUE(fileContains("src/loop_functions.cpp", "[CET];skip;why;bad_payload;src;", &pos));
    TEST_ASSERT_TRUE(fileContains("src/loop_functions.cpp", "[CET];hold;why;rank;by;", &pos));
    // under --timesrc (INSTRUMENT) the set is discarded: it must not be noted either
    TEST_ASSERT_TRUE(fileContains("src/loop_functions.cpp", "if(clockInstrGated()) return;", &pos));
    // live GPS module time without a fix counts as a live GPS rank
    TEST_ASSERT_TRUE(fileContains("src/loop_functions.cpp", "WZ_GPS_TimeLive()", &pos));
    TEST_ASSERT_TRUE(fileContains("src/gps_functions.cpp", "bool WZ_GPS_TimeLive()", &pos));
}

// phone set the clock; inside its holdover the next CET is held, after it the beacon is filtered
// (not FIRST, no free step back). NTP/GPS never expire: see test_rank_ntp_never_expires.
void test_rank_ntp_lost_then_cet_behind_holds(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    timeRankNoteRef(&s, &f, TR_MANUAL, 1000);       // phone set; no CET seen since boot
    CetGateOut o = cetBeaconGate(&s, &f, TR_NONE, true, true, -8, 600000UL);   // 10 min later, CET 8 s behind
    TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
    TEST_ASSERT_EQUAL(TR_MANUAL, o.by);
    // after the holdover: filtered, a backward beacon is BACK, not FIRST
    o = cetBeaconGate(&s, &f, TR_NONE, true, true, -8, H1 + 5000UL);
    TEST_ASSERT_EQUAL(CETG_FILTER, o.action);
    TEST_ASSERT_EQUAL(CET_V_BACK, o.v);
}

void test_rank_ref_on_higher_set_only(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    timeRankNoteRef(&s, &f, TR_CET, 5);
    TEST_ASSERT_FALSE(f.have_ref);
    timeRankNoteRef(&s, &f, TR_GPS, 6);
    TEST_ASSERT_TRUE(f.have_ref);
}

void test_rank_live_flag_blocks(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    CetGateOut o = cetBeaconGate(&s, &f, TR_GPS, true, true, 5, 100);
    TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
    TEST_ASSERT_EQUAL(TR_GPS, o.by);
    TEST_ASSERT_TRUE(f.have_ref);
}

// premium sources: NTP set, NTP lost, CET 7 h later (8 s ahead or behind): hold
void test_rank_ntp_never_expires(void)
{
    const uint32_t t7h = 7UL * H1;
    for(int d = -8; d <= 8; d += 16)
    {
        TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
        timeRankNoteRef(&s, &f, TR_NTP, 1000);
        CetGateOut o = cetBeaconGate(&s, &f, TR_NONE, true, true, d, 1000 + t7h);
        TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
        TEST_ASSERT_EQUAL(TR_NTP, o.by);
    }
}

void test_rank_gps_never_expires(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    timeRankNoteRef(&s, &f, TR_GPS, 1000);
    CetGateOut o = cetBeaconGate(&s, &f, TR_NONE, true, true, 8, 1000 + 7UL * H1);
    TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
    TEST_ASSERT_EQUAL(TR_GPS, o.by);
    o = cetBeaconGate(&s, &f, TR_NONE, true, true, -8, 1000 + 30UL * H1);
    TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
}

// phone keeps its 1 h holdover: 61 min later {CET} is filtered (fwd applies, back is BACK)
void test_rank_phone_holdover_1h(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    timeRankNoteRef(&s, &f, TR_MANUAL, 1000);
    CetGateOut o = cetBeaconGate(&s, &f, TR_NONE, true, true, 8, 1000 + 61UL * 60UL * 1000UL);
    TEST_ASSERT_EQUAL(CETG_FILTER, o.action);
    TEST_ASSERT_EQUAL(CET_V_FWD, o.v);
    o = cetBeaconGate(&s, &f, TR_NONE, true, true, -8, 1000 + 62UL * 60UL * 1000UL);
    TEST_ASSERT_EQUAL(CETG_FILTER, o.action);
    TEST_ASSERT_EQUAL(CET_V_BACK, o.v);
    // inside the hour it still holds
    TimeRankState s2 = {}; CetFilter f2; cetFilterInit(&f2);
    timeRankNoteRef(&s2, &f2, TR_MANUAL, 1000);
    o = cetBeaconGate(&s2, &f2, TR_NONE, true, true, 8, 1000 + 59UL * 60UL * 1000UL);
    TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
    TEST_ASSERT_EQUAL(TR_MANUAL, o.by);
}

// NTP outranks GPS in the gate, for live flags and for noted sources alike
void test_rank_ntp_outranks_gps(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    timeRankNoteRef(&s, &f, TR_NTP, 10);
    timeRankNoteRef(&s, &f, TR_GPS, 20);
    CetGateOut o = cetBeaconGate(&s, &f, TR_GPS, true, true, 5, 100);   // GPS live, NTP noted
    TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
    TEST_ASSERT_EQUAL(TR_NTP, o.by);
    TimeRankState e = {}; CetFilter g; cetFilterInit(&g);
    o = cetBeaconGate(&e, &g, TR_NTP, true, true, 5, 100);               // NTP live
    TEST_ASSERT_EQUAL(TR_NTP, o.by);
    TimeRankState e2 = {}; CetFilter g2; cetFilterInit(&g2);
    timeRankNoteRef(&e2, &g2, TR_GPS, 5);
    o = cetBeaconGate(&e2, &g2, TR_RTC, true, true, 5, 100);             // RTC live, GPS noted
    TEST_ASSERT_EQUAL(TR_GPS, o.by);
}

// GPS module time valid without a fix: the loop passes TR_GPS as the live rank; CET holds
void test_rank_gps_time_without_fix_holds(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    CetGateOut o = cetBeaconGate(&s, &f, TR_GPS, true, true, 8, 100);
    TEST_ASSERT_EQUAL(CETG_HOLD_RANK, o.action);
    TEST_ASSERT_EQUAL(TR_GPS, o.by);
}

// --timesrc cet: NTP/RTC sets repeat every pass but are discarded and not noted; a quorum must
// still form. The set only refreshes have_ref, never n_back.
void test_rank_set_keeps_quorum(void)
{
    TimeRankState bypass = {}, notes = {}; CetFilter f; cetFilterInit(&f);
    timeRankNoteRef(&notes, &f, TR_NTP, 1);
    CetGateOut o = cetBeaconGate(&bypass, &f, TR_NONE, true, true, -8, 1000);
    TEST_ASSERT_EQUAL(CET_V_BACK, o.v);
    timeRankNoteRef(&notes, &f, TR_NTP, 2000);   // repeated NTP set must not clear the quorum
    TEST_ASSERT_EQUAL_UINT8(1, f.n_back);
    o = cetBeaconGate(&bypass, &f, TR_NONE, true, true, -8, 3000);
    TEST_ASSERT_EQUAL(CET_V_BACK, o.v);
    o = cetBeaconGate(&bypass, &f, TR_NONE, true, true, -8, 4000);
    TEST_ASSERT_EQUAL(CETG_FILTER, o.action);
    TEST_ASSERT_EQUAL(CET_V_QUORUM, o.v);
}

// millis wrap hardening: noting any source clears holdover bits that have run out
void test_rank_note_clears_expired(void)
{
    TimeRankState s = {};
    timeRankNote(&s, TR_MANUAL, 0);
    timeRankNote(&s, TR_RTC, H1 + 5);
    TEST_ASSERT_EQUAL_UINT8(0, s.seen & (1u << TR_MANUAL));
    TEST_ASSERT_NOT_EQUAL(0, s.seen & (1u << TR_RTC));
    timeRankNote(&s, TR_NTP, 3UL * H6);   // RTC (6 h) expired too; NTP never does
    TEST_ASSERT_EQUAL_UINT8(0, s.seen & (1u << TR_RTC));
    TEST_ASSERT_NOT_EQUAL(0, s.seen & (1u << TR_NTP));
}

void test_cet_bad_payload_skips(void)
{
    TimeRankState s = {}; CetFilter f; cetFilterInit(&f);
    CetGateOut o = cetBeaconGate(&s, &f, TR_NONE, false, true, 0, 100);
    TEST_ASSERT_EQUAL(CETG_BAD_PAYLOAD, o.action);
    TEST_ASSERT_FALSE(f.have_ref);   // does not count as a first beacon
    // clock never set but payload fine: noteRef and apply as before
    o = cetBeaconGate(&s, &f, TR_NONE, true, false, 0, 200);
    TEST_ASSERT_EQUAL(CETG_FILTER, o.action);
    TEST_ASSERT_EQUAL(CET_V_FIRST, o.v);
    TEST_ASSERT_TRUE(f.have_ref);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_valid_known_epoch);
    RUN_TEST(test_parse_leap_day);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_age_sign);
    RUN_TEST(test_trusted_truth_table);
    RUN_TEST(test_find_in_frame);
    RUN_TEST(test_frame_stale_truth_table);
    RUN_TEST(test_step_guard);
    RUN_TEST(test_filter_first_any_direction);
    RUN_TEST(test_filter_forward);
    RUN_TEST(test_filter_band);
    RUN_TEST(test_filter_quorum);
    RUN_TEST(test_filter_quorum_needs_consecutive);
    RUN_TEST(test_filter_fwd_resets_quorum);
    RUN_TEST(test_filter_quorum_window);
    RUN_TEST(test_filter_millis_wrap);
    RUN_TEST(test_filter_replay_tbeam_s3);
    RUN_TEST(test_filter_replay_heltec_s4);
    RUN_TEST(test_old_rule_oracle_tbeam);
    RUN_TEST(test_rank_of);
    RUN_TEST(test_rank_holdover_edges);
    RUN_TEST(test_rank_highest_wins);
    RUN_TEST(test_rank_cet_never_blocks);
    RUN_TEST(test_rank_wrap);
    RUN_TEST(test_rank_hook_anchor);
    RUN_TEST(test_rank_ntp_lost_then_cet_behind_holds);
    RUN_TEST(test_rank_ref_on_higher_set_only);
    RUN_TEST(test_rank_live_flag_blocks);
    RUN_TEST(test_rank_ntp_never_expires);
    RUN_TEST(test_rank_gps_never_expires);
    RUN_TEST(test_rank_phone_holdover_1h);
    RUN_TEST(test_rank_ntp_outranks_gps);
    RUN_TEST(test_rank_gps_time_without_fix_holds);
    RUN_TEST(test_rank_set_keeps_quorum);
    RUN_TEST(test_rank_note_clears_expired);
    RUN_TEST(test_cet_bad_payload_skips);
    return UNITY_END();
}
