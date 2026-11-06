// Unity tests of src/fw_schedule.h: planned install time and countdown notice of the auto update.
// Host only (env native_au_schedule). The module is pure; the tests also pin by source text that it cannot start
// an install and that the AU_AUTO_INSTALL guard of fw_update.h (decision D14) is intact.
#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <string>
#include "../../src/fw_schedule.h"
#include "../../src/web_functions/au_ui_state.h"

void setUp(void) {}
void tearDown(void) {}

using namespace fw_sched_detail;

static std::string readRepoFile(const char *rel); // below

static int64_t T(int y, int mo, int d, int h, int mi)
{
    int64_t v = 0;
    TEST_ASSERT_TRUE(fwSchedParseUtc(y, mo, d, h, mi, v));
    return v;
}

// EU rule (CET/CEST): summer time from the last Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC.
static int64_t lastSunday(int y, unsigned m)
{
    const int64_t d31 = daysFromCivil(y, m, 31);
    return d31 - (weekdayOfDay(d31) % 7);
}
static int32_t euOff(int64_t utc, void *)
{
    int64_t y;
    unsigned m, d;
    civilFromDays(floorDiv(utc, 86400), y, m, d);
    const int64_t s = lastSunday((int)y, 3) * 86400 + 3600;
    const int64_t e = lastSunday((int)y, 10) * 86400 + 3600;
    return (utc >= s && utc < e) ? 7200 : 3600;
}
static int32_t utcOff(int64_t, void *) { return 0; }

static FwSchedIn mk(int64_t rel, int delay, int wd, int minute, int64_t now = 0)
{
    FwSchedIn in;
    memset(&in, 0, sizeof(in));
    in.haveRelease = true;
    in.releaseUtc = rel;
    in.clockValid = true;
    in.nowUtc = now;
    in.delayDays = delay;
    in.weekday = wd;
    in.minuteOfDay = minute;
    in.off = euOff;
    return in;
}

static FwSchedOut run(const FwSchedIn &in)
{
    FwSchedOut o;
    fwSchedCompute(in, o);
    return o;
}

static std::string when(const FwSchedIn &in, int64_t utc)
{
    char b[32];
    fwSchedFormatWhen(in, utc, b, sizeof(b));
    return b;
}

// ---- calendar helpers -------------------------------------------------------------------------------

void test_calendar_helpers(void)
{
    TEST_ASSERT_EQUAL_INT64(0, daysFromCivil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT(4, weekdayOfDay(0)); // Thursday
    TEST_ASSERT_EQUAL_INT64(1782864000, daysFromCivil(2026, 7, 1) * 86400);
    int64_t y;
    unsigned m, d;
    civilFromDays(daysFromCivil(2028, 2, 29), y, m, d);
    TEST_ASSERT_TRUE(y == 2028 && m == 2 && d == 29);
    TEST_ASSERT_EQUAL_UINT(29, monthDays(2028, 2));
    TEST_ASSERT_EQUAL_UINT(28, monthDays(2100, 2)); // not a leap year
    TEST_ASSERT_EQUAL_UINT(29, monthDays(2000, 2));
    TEST_ASSERT_EQUAL_INT64(-1, floorDiv(-1, 86400));
}

// ---- states ------------------------------------------------------------------------------------------

void test_no_candidate(void)
{
    FwSchedIn in = mk(T(2026, 11, 5, 12, 0), 3, 0, 180);
    in.haveRelease = false;
    const FwSchedOut o = run(in);
    TEST_ASSERT_EQUAL_INT(FWSCHED_NO_CANDIDATE, o.state);
    TEST_ASSERT_EQUAL_INT64(0, o.installUtc);
    char b[200];
    TEST_ASSERT_EQUAL_UINT(0, fwSchedNotice(in, o, b, sizeof(b)));
    TEST_ASSERT_EQUAL_STRING("", b);
}

void test_clock_invalid(void)
{
    FwSchedIn in = mk(T(2026, 11, 5, 12, 0), 3, 0, 180);
    in.clockValid = false;
    const FwSchedOut o = run(in);
    TEST_ASSERT_EQUAL_INT(FWSCHED_CLOCK_INVALID, o.state);
    TEST_ASSERT_TRUE(o.installUtc != 0); // the plan is known, only the countdown is not
    char b[200];
    fwSchedNotice(in, o, b, sizeof(b));
    TEST_ASSERT_NOT_NULL(strstr(b, "clock is not set"));
    TEST_ASSERT_NOT_NULL(strstr(b, "not active in this firmware"));
}

void test_waiting_then_due_boundary(void)
{
    FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 3, 0, 180);
    const FwSchedOut o = run(in);
    // t0 = Sat 7 Nov 12:00 UTC, 03:00 CET = 02:00 UTC: Sun 8 Nov
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 8, 2, 0), o.installUtc);
    in.nowUtc = o.installUtc - 1;
    TEST_ASSERT_EQUAL_INT(FWSCHED_WAITING, run(in).state);
    in.nowUtc = o.installUtc;
    TEST_ASSERT_EQUAL_INT(FWSCHED_DUE, run(in).state);
    in.nowUtc = o.installUtc + 86400 * 30;
    TEST_ASSERT_EQUAL_INT(FWSCHED_DUE, run(in).state);
}

void test_days_and_hours_left(void)
{
    FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 3, 0, 180);
    const int64_t at = T(2026, 11, 8, 2, 0);
    in.nowUtc = at - (3 * 86400 + 3600 + 50 * 60); // 3 d 1 h 50 min before
    FwSchedOut o = run(in);
    TEST_ASSERT_EQUAL_INT(3, o.daysLeft);
    TEST_ASSERT_EQUAL_INT(1, o.hoursLeft);
    in.nowUtc = at - (5 * 3600 + 10);
    o = run(in);
    TEST_ASSERT_EQUAL_INT(0, o.daysLeft);
    TEST_ASSERT_EQUAL_INT(5, o.hoursLeft);
    in.nowUtc = at - 600;
    o = run(in);
    TEST_ASSERT_TRUE(o.daysLeft == 0 && o.hoursLeft == 0 && o.state == FWSCHED_WAITING);
    in.nowUtc = at - 86400;
    o = run(in);
    TEST_ASSERT_TRUE(o.daysLeft == 1 && o.hoursLeft == 0);
}

// ---- delay mode edge cases ---------------------------------------------------------------------------

void test_delay_zero_is_next_night(void)
{
    // release 14:00 UTC (15:00 local): 03:00 local of the NEXT day, 02:00 UTC
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 6, 2, 0), run(mk(T(2026, 11, 5, 14, 0), 0, 0, 180)).installUtc);
}

void test_release_before_install_time_same_day(void)
{
    // release 00:30 UTC = 01:30 local, install time 03:00 local still ahead: the same night
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 5, 2, 0), run(mk(T(2026, 11, 5, 0, 30), 0, 0, 180)).installUtc);
}

void test_release_after_install_time_same_day(void)
{
    // release 08:00 UTC = 09:00 local, 03:00 local already past: the next day
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 6, 2, 0), run(mk(T(2026, 11, 5, 8, 0), 0, 0, 180)).installUtc);
}

void test_release_exactly_at_install_time_counts(void)
{
    // "on or after": the instant itself is the plan in delay mode
    const int64_t at = T(2026, 11, 5, 2, 0);
    TEST_ASSERT_EQUAL_INT64(at, run(mk(at, 0, 0, 180)).installUtc);
}

void test_delay_seven(void)
{
    // 7 days after 2026-11-04 12:00 = 11-11 12:00 UTC, next 03:00 local = 11-12 02:00 UTC
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 12, 2, 0), run(mk(T(2026, 11, 4, 12, 0), 7, 0, 180)).installUtc);
}

void test_install_time_adjustable(void)
{
    // 22:15 local = 21:15 UTC on the first day at or after release + 1 d
    const FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 1, 0, 22 * 60 + 15);
    const FwSchedOut o = run(in);
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 5, 21, 15), o.installUtc);
    TEST_ASSERT_EQUAL_STRING("Thu 5 Nov 22:15", when(in, o.installUtc).c_str());
}

void test_month_rollover(void)
{
    // release Fri 2026-01-30 12:00 UTC + 3 d = 02-02 12:00 -> next 03:00 = 02-03 (Tue)
    const FwSchedIn in = mk(T(2026, 1, 30, 12, 0), 3, 0, 180);
    const FwSchedOut o = run(in);
    TEST_ASSERT_EQUAL_INT64(T(2026, 2, 3, 2, 0), o.installUtc);
    TEST_ASSERT_EQUAL_STRING("Tue 3 Feb 03:00", when(in, o.installUtc).c_str());
}

void test_year_rollover(void)
{
    // 2026-12-30 12:00 + 3 d = 2027-01-02 12:00 UTC -> 2027-01-03 03:00 local (Sunday)
    const FwSchedIn in = mk(T(2026, 12, 30, 12, 0), 3, 0, 180);
    const FwSchedOut o = run(in);
    TEST_ASSERT_EQUAL_INT64(T(2027, 1, 3, 2, 0), o.installUtc);
    TEST_ASSERT_EQUAL_STRING("Sun 3 Jan 03:00", when(in, o.installUtc).c_str());
}

void test_leap_day(void)
{
    // 2028-02-27 12:00 + 2 d = 02-29 12:00 UTC -> 03-01 03:00 local (Wednesday), the 29th is a real day
    const FwSchedIn in = mk(T(2028, 2, 27, 12, 0), 2, 0, 180);
    const FwSchedOut o = run(in);
    TEST_ASSERT_EQUAL_INT64(T(2028, 3, 1, 2, 0), o.installUtc);
    TEST_ASSERT_EQUAL_STRING("Wed 1 Mar 03:00", when(in, o.installUtc).c_str());
    // delay 1 from 02-27 lands on the leap day itself: 02-28 12:00 -> 02-29 03:00
    TEST_ASSERT_EQUAL_INT64(T(2028, 2, 29, 2, 0), run(mk(T(2028, 2, 27, 12, 0), 1, 0, 180)).installUtc);
    // 2027 has no Feb 29: 02-28 + 1 d goes to 03-01
    TEST_ASSERT_EQUAL_INT64(T(2027, 3, 1, 2, 0), run(mk(T(2027, 2, 27, 12, 0), 1, 0, 180)).installUtc);
}

// ---- DST ---------------------------------------------------------------------------------------------

void test_dst_spring_forward(void)
{
    // 2026-03-29: 02:00 CET -> 03:00 CEST. 03:00 local that day is 01:00 UTC (CEST).
    const FwSchedIn a = mk(T(2026, 3, 28, 12, 0), 0, 0, 180);
    TEST_ASSERT_EQUAL_INT64(T(2026, 3, 29, 1, 0), run(a).installUtc);
    TEST_ASSERT_EQUAL_STRING("Sun 29 Mar 03:00", when(a, run(a).installUtc).c_str());
    // 02:30 does not exist that day: it lands after the gap, 03:30 CEST = 01:30 UTC
    const FwSchedIn g = mk(T(2026, 3, 28, 12, 0), 0, 0, 150);
    TEST_ASSERT_EQUAL_INT64(T(2026, 3, 29, 1, 30), run(g).installUtc);
    TEST_ASSERT_EQUAL_STRING("Sun 29 Mar 03:30", when(g, run(g).installUtc).c_str());
}

void test_dst_fall_back(void)
{
    // 2026-10-25: 03:00 CEST -> 02:00 CET. 02:30 local happens twice; the earlier one is 00:30 UTC.
    TEST_ASSERT_EQUAL_INT64(T(2026, 10, 25, 0, 30), run(mk(T(2026, 10, 24, 12, 0), 0, 0, 150)).installUtc);
    // 03:00 local that day is already CET: 02:00 UTC
    TEST_ASSERT_EQUAL_INT64(T(2026, 10, 25, 2, 0), run(mk(T(2026, 10, 24, 12, 0), 0, 0, 180)).installUtc);
}

void test_dst_day_length_does_not_skip_or_repeat(void)
{
    // the day after the change is exactly one install per local day
    const int64_t a = run(mk(T(2026, 10, 25, 3, 0), 0, 0, 180)).installUtc; // release after the 25th 03:00 local
    TEST_ASSERT_EQUAL_INT64(T(2026, 10, 26, 2, 0), a);
    const int64_t b = run(mk(T(2026, 3, 29, 2, 0), 0, 0, 180)).installUtc; // release after that day's 03:00 CEST = 01:00 UTC
    TEST_ASSERT_EQUAL_INT64(T(2026, 3, 30, 1, 0), b);
}

void test_plain_utc_converter(void)
{
    FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 3, 0, 180);
    in.off = utcOff;
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 8, 3, 0), run(in).installUtc);
    in.off = nullptr; // no converter = UTC
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 8, 3, 0), run(in).installUtc);
}

// ---- weekday mode ------------------------------------------------------------------------------------

void test_weekday_basic(void)
{
    // release Thu 2026-11-05 12:00 UTC
    const int64_t rel = T(2026, 11, 5, 12, 0);
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 8, 2, 0), run(mk(rel, 3, 7, 180)).installUtc);  // Sun
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 6, 2, 0), run(mk(rel, 3, 5, 180)).installUtc);  // Fri: the delay is not used
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 9, 2, 0), run(mk(rel, 0, 1, 180)).installUtc);  // Mon
}

void test_weekday_same_day_only_if_still_ahead(void)
{
    // Thu 00:30 UTC = 01:30 local; install time Thu 03:00 local is ahead: same day counts
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 5, 2, 0), run(mk(T(2026, 11, 5, 0, 30), 3, 4, 180)).installUtc);
    // Thu 12:00 UTC: already past, next Thursday
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 12, 2, 0), run(mk(T(2026, 11, 5, 12, 0), 3, 4, 180)).installUtc);
    // release at the install instant itself: strictly after, so the next week
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 12, 2, 0), run(mk(T(2026, 11, 5, 2, 0), 3, 4, 180)).installUtc);
}

void test_weekday_never_later_than_seven_days(void)
{
    int count = 0;
    for (int d = 0; d < 400; d += 3)
        for (int h = 0; h < 24; h += 5)
            for (int wd = 1; wd <= 7; wd++)
                for (int minute = 0; minute < 1440; minute += 360)
                {
                    const int64_t rel = T(2026, 1, 1, h, 0) + (int64_t)d * 86400;
                    const FwSchedOut o = run(mk(rel, 0, wd, minute));
                    TEST_ASSERT_TRUE(o.installUtc > rel);
                    TEST_ASSERT_TRUE(o.installUtc <= rel + 7 * 86400 + 3600); // +1 h across a DST change
                    count++;
                }
    TEST_ASSERT_TRUE(count > 1000);
}

void test_weekday_lands_on_that_weekday(void)
{
    for (int wd = 1; wd <= 7; wd++)
    {
        const FwSchedIn in = mk(T(2026, 11, 5, 12, 0), 0, wd, 180);
        const int64_t u = run(in).installUtc;
        const int64_t local = u + euOff(u, nullptr);
        TEST_ASSERT_EQUAL_INT(wd, weekdayOfDay(floorDiv(local, 86400)));
        TEST_ASSERT_EQUAL_INT(180, (int)(floorMod(local, 86400) / 60));
    }
}

// ---- clamping ----------------------------------------------------------------------------------------

void test_invalid_inputs_are_clamped(void)
{
    const int64_t rel = T(2026, 11, 4, 12, 0);
    TEST_ASSERT_EQUAL_INT64(run(mk(rel, 7, 0, 180)).installUtc, run(mk(rel, 99, 0, 180)).installUtc);
    TEST_ASSERT_EQUAL_INT64(run(mk(rel, 0, 0, 180)).installUtc, run(mk(rel, -5, 0, 180)).installUtc);
    TEST_ASSERT_EQUAL_INT64(run(mk(rel, 3, 0, 180)).installUtc, run(mk(rel, 3, 9, 180)).installUtc);  // weekday 9 = none
    TEST_ASSERT_EQUAL_INT64(run(mk(rel, 3, 0, 180)).installUtc, run(mk(rel, 3, -1, 180)).installUtc);
    TEST_ASSERT_EQUAL_INT64(run(mk(rel, 3, 0, 1439)).installUtc, run(mk(rel, 3, 0, 5000)).installUtc);
    TEST_ASSERT_EQUAL_INT64(run(mk(rel, 3, 0, 0)).installUtc, run(mk(rel, 3, 0, -40)).installUtc);
}

// ---- release instant from the tag date ---------------------------------------------------------------

void test_release_from_tag_date(void)
{
    int64_t r = 0;
    // now 2026-11-06: tag 11.05 is yesterday, noon UTC
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(11, 5, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 5, 12, 0), r);
    // an older release of this year
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(2, 3, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2026, 2, 3, 12, 0), r);
    // year boundary: now Jan 2, tag Dec 30 belongs to last year
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(12, 30, T(2027, 1, 2, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2026, 12, 30, 12, 0), r);
    // year boundary the other way: now Dec 31 (UTC), tag Jan 1 is at most a day ahead (clock zone), next year
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(1, 1, T(2026, 12, 31, 23, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2027, 1, 1, 12, 0), r);
    // today
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(11, 6, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 6, 12, 0), r);
    // a tag date further ahead than two days is last year's
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(11, 10, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2025, 11, 10, 12, 0), r);
}

void test_release_from_tag_leap_and_invalid(void)
{
    int64_t r = 0;
    // Feb 29 seen in 2026: the last leap year is 2024
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(2, 29, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2024, 2, 29, 12, 0), r);
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(2, 29, T(2028, 3, 1, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2028, 2, 29, 12, 0), r);
    TEST_ASSERT_FALSE(fwSchedReleaseFromMd(2, 30, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_FALSE(fwSchedReleaseFromMd(13, 1, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_FALSE(fwSchedReleaseFromMd(0, 1, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_FALSE(fwSchedReleaseFromMd(4, 31, T(2026, 11, 6, 9, 0), r));
}

void test_release_from_tag_plan_delay_three(void)
{
    // tag 11.05: noon UTC on the 5th + 3 d = 8th 12:00 -> the night of the 9th, 03:00 local
    int64_t r = 0;
    TEST_ASSERT_TRUE(fwSchedReleaseFromMd(11, 5, T(2026, 11, 6, 9, 0), r));
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 9, 2, 0), run(mk(r, 3, 0, 180)).installUtc);
    // delay 0: the night after the release day
    TEST_ASSERT_EQUAL_INT64(T(2026, 11, 6, 2, 0), run(mk(r, 0, 0, 180)).installUtc);
}

void test_parse_utc(void)
{
    int64_t v;
    TEST_ASSERT_TRUE(fwSchedParseUtc(2026, 11, 5, 9, 30, v));
    TEST_ASSERT_EQUAL_INT64(daysFromCivil(2026, 11, 5) * 86400 + 9 * 3600 + 30 * 60, v);
    TEST_ASSERT_FALSE(fwSchedParseUtc(2026, 2, 29, 0, 0, v));
    TEST_ASSERT_TRUE(fwSchedParseUtc(2028, 2, 29, 23, 59, v));
    TEST_ASSERT_FALSE(fwSchedParseUtc(2026, 11, 5, 24, 0, v));
    TEST_ASSERT_FALSE(fwSchedParseUtc(2026, 11, 5, 9, 60, v));
    TEST_ASSERT_FALSE(fwSchedParseUtc(2026, 13, 5, 9, 0, v));
    TEST_ASSERT_FALSE(fwSchedParseUtc(1969, 12, 31, 9, 0, v));
}

// ---- notice text -------------------------------------------------------------------------------------

void test_notice_text_exact(void)
{
    // the operator's example: planned Sun 8 Nov 03:00 local, 3 days ahead
    FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 3, 0, 180, T(2026, 11, 5, 1, 0));
    FwSchedOut o = run(in);
    char b[200];
    fwSchedNotice(in, o, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("Planned install: Sun 8 Nov 03:00 (in 3 days). Automatic install is not active in this "
                             "firmware; use Download or Install on the Info page.",
                             b);
    in.nowUtc = T(2026, 11, 7, 2, 0); // 1 day
    fwSchedNotice(in, run(in), b, sizeof(b));
    TEST_ASSERT_NOT_NULL(strstr(b, "(in 1 day)."));
    in.nowUtc = T(2026, 11, 7, 22, 0); // 4 h
    fwSchedNotice(in, run(in), b, sizeof(b));
    TEST_ASSERT_NOT_NULL(strstr(b, "(in 4 hours)."));
    in.nowUtc = T(2026, 11, 8, 1, 30);
    fwSchedNotice(in, run(in), b, sizeof(b));
    TEST_ASSERT_NOT_NULL(strstr(b, "(in less than 1 hour)."));
    in.nowUtc = T(2026, 11, 8, 2, 0);
    o = run(in);
    fwSchedNotice(in, o, b, sizeof(b));
    TEST_ASSERT_EQUAL_STRING("Planned install time reached: Sun 8 Nov 03:00. Automatic install is not active in this "
                             "firmware; use Download or Install on the Info page.",
                             b);
}

void test_notice_is_honest_in_every_state(void)
{
    // every non-empty notice says that the install is not automatic and never promises one
    const FwSchedState want[] = {FWSCHED_CLOCK_INVALID, FWSCHED_WAITING, FWSCHED_DUE};
    for (int k = 0; k < 3; k++)
    {
        FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 3, 0, 180, T(2026, 11, 5, 1, 0));
        if (k == 0)
            in.clockValid = false;
        if (k == 2)
            in.nowUtc = T(2026, 12, 1, 0, 0);
        const FwSchedOut o = run(in);
        TEST_ASSERT_EQUAL_INT(want[k], o.state);
        char b[200];
        fwSchedNotice(in, o, b, sizeof(b));
        TEST_ASSERT_NOT_NULL(strstr(b, "Automatic install is not active in this firmware; use Download or Install on the Info page."));
        TEST_ASSERT_NULL(strstr(b, "will install"));
        TEST_ASSERT_NULL(strstr(b, "installing"));
        TEST_ASSERT_NULL(strstr(b, "starts automatically"));
    }
}

void test_notice_truncation_is_safe(void)
{
    FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 3, 0, 180, T(2026, 11, 5, 1, 0));
    const FwSchedOut o = run(in);
    char b[20];
    const size_t n = fwSchedNotice(in, o, b, sizeof(b));
    TEST_ASSERT_EQUAL_UINT(strlen(b), n);
    TEST_ASSERT_TRUE(n < sizeof(b));
    TEST_ASSERT_EQUAL_UINT(0, fwSchedNotice(in, o, nullptr, 0));
    char full[200];
    TEST_ASSERT_TRUE(fwSchedNotice(in, o, full, sizeof(full)) < FWSCHED_NOTICE_MAX); // the buffers of the callers
}

void test_notice_simulated_prefix(void)
{
    FwSchedIn in = mk(T(2026, 11, 4, 12, 0), 3, 0, 180, T(2026, 11, 5, 1, 0));
    const FwSchedOut o = run(in);
    char a[200], b[200];
    fwSchedNotice(in, o, a, sizeof(a), false);
    fwSchedNotice(in, o, b, sizeof(b), true);
    TEST_ASSERT_EQUAL_STRING_LEN("[simulated] ", b, 12);
    TEST_ASSERT_EQUAL_STRING(a, b + 12);
    TEST_ASSERT_NULL(strstr(a, "[simulated]"));
    // no candidate: empty either way, nothing to mark
    FwSchedIn none = in;
    none.haveRelease = false;
    TEST_ASSERT_EQUAL_UINT(0, fwSchedNotice(none, run(none), b, sizeof(b), true));
    TEST_ASSERT_EQUAL_STRING("", b);
    // truncation keeps the prefix path safe too
    char s[20];
    const size_t n = fwSchedNotice(in, o, s, sizeof(s), true);
    TEST_ASSERT_EQUAL_UINT(strlen(s), n);
    TEST_ASSERT_TRUE(n < sizeof(s));
}

void test_notice_worst_case_fits(void)
{
    // the longest text over all states, weekdays, times and "left" counts must fit the callers' buffer
    // (FwSchedView.text, FWSCHED_NOTICE_MAX) together with "[simulated] "
    size_t longest = 0;
    for (int state = 0; state < 3; state++)
        for (int left = 0; left < 3; left++)
            for (int day = 0; day < 31; day += 5)
            {
                FwSchedIn in = mk(T(2026, 3, 1 + day, 12, 0), 7, 0, 23 * 60 + 59, 0);
                in.clockValid = state != 0;
                in.nowUtc = state == 2 ? T(2030, 1, 1, 0, 0) : (left == 0 ? T(2026, 3, 1 + day, 12, 0) : (left == 1 ? T(2026, 3, 1 + day, 12, 0) + 8 * 86400 - 3 * 3600 : T(2026, 3, 1 + day, 12, 0) + 8 * 86400 - 1800));
                const FwSchedOut o = run(in);
                char b[FWSCHED_NOTICE_MAX + 64];
                const size_t n = fwSchedNotice(in, o, b, sizeof(b), true);
                if (n > longest)
                    longest = n;
            }
    TEST_ASSERT_TRUE_MESSAGE(longest > 100, "worst-case probe did not produce long texts");
    TEST_ASSERT_TRUE_MESSAGE(longest < FWSCHED_NOTICE_MAX, "the notice no longer fits FwSchedView.text");
}

// ---- persisted settings: defaults, schema, load repair -----------------------------------------------

void test_sanitize_repairs_to_defaults(void)
{
    int d = -1, w = 9, m = 5000;
    TEST_ASSERT_EQUAL_INT(3, fwSchedSanitize(d, w, m));
    TEST_ASSERT_EQUAL_INT(FWSCHED_DELAY_DEFAULT, d);
    TEST_ASSERT_EQUAL_INT(FWSCHED_DAY_NONE, w);
    TEST_ASSERT_EQUAL_INT(FWSCHED_TIME_DEFAULT, m);
    TEST_ASSERT_EQUAL_INT(0, fwSchedSanitize(d, w, m)); // idempotent
    d = 8;
    w = -1;
    m = 1440;
    TEST_ASSERT_EQUAL_INT(3, fwSchedSanitize(d, w, m));
    int d2 = 0, w2 = 7, m2 = 1439; // the edges stay
    TEST_ASSERT_EQUAL_INT(0, fwSchedSanitize(d2, w2, m2));
    TEST_ASSERT_EQUAL_INT(0, d2);
    TEST_ASSERT_EQUAL_INT(7, w2);
    TEST_ASSERT_EQUAL_INT(1439, m2);
    int dn = -1, wn = 3, mn = 0; // only the bad one is replaced
    TEST_ASSERT_EQUAL_INT(1, fwSchedSanitize(dn, wn, mn));
    TEST_ASSERT_EQUAL_INT(3, dn);
    TEST_ASSERT_EQUAL_INT(3, wn);
    TEST_ASSERT_EQUAL_INT(0, mn);
}

static std::string rowOf(const std::string &cfg, const char *name)
{
    const size_t at = cfg.find(std::string("X(\"") + name + "\"");
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, name);
    return cfg.substr(at, cfg.find('\n', at) - at);
}

void test_settings_defaults_and_schema_are_pinned(void)
{
    // a key missing from stored data keeps the compiled-in default (generic walk / import): these are the defaults
    const std::string set = readRepoFile("src/meshcom_settings.h");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_audelay, 3)") != std::string::npos, "node_audelay default is not 3");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_auday, 0)") != std::string::npos, "node_auday default is not 0");
    TEST_ASSERT_TRUE_MESSAGE(set.find("M(int, node_autime, 180)") != std::string::npos, "node_autime default is not 180");
    TEST_ASSERT_EQUAL_INT(3, FWSCHED_DELAY_DEFAULT);
    TEST_ASSERT_EQUAL_INT(0, FWSCHED_DAY_NONE);
    TEST_ASSERT_EQUAL_INT(180, FWSCHED_TIME_DEFAULT);
    TEST_ASSERT_EQUAL_INT(7, FWSCHED_DELAY_MAX);
    // NVS key limit: 15 characters
    TEST_ASSERT_TRUE(strlen("node_audelay") <= 15 && strlen("node_auday") <= 15 && strlen("node_autime") <= 15);
    // decode bounds of the schema (the import and the generic loader refuse values outside)
    const std::string cfg = readRepoFile("src/config_json.h");
    const std::string rd = rowOf(cfg, "node_audelay"), ra = rowOf(cfg, "node_auday"), rt = rowOf(cfg, "node_autime");
    TEST_ASSERT_TRUE(rd.find("CFG_INT") != std::string::npos && rd.find("0.0, 7.0") != std::string::npos);
    TEST_ASSERT_TRUE(ra.find("CFG_INT") != std::string::npos && ra.find("0.0, 7.0") != std::string::npos);
    TEST_ASSERT_TRUE(rt.find("CFG_INT") != std::string::npos && rt.find("0.0, 1439.0") != std::string::npos);
    // the ESP32 load path repairs them (no decode() clamp there, unlike nRF52)
    const std::string fl = readRepoFile("src/esp32/esp32_flash.cpp");
    const size_t at = fl.find("bool sanitize_loaded_settings(void)");
    TEST_ASSERT_TRUE(at != std::string::npos);
    TEST_ASSERT_TRUE_MESSAGE(fl.find("fwSchedSanitize(meshcom_settings.node_audelay, meshcom_settings.node_auday, meshcom_settings.node_autime)", at) != std::string::npos,
                             "sanitize_loaded_settings does not repair the schedule settings");
}

// ---- /austatus "sched" key (auAppendSched) -----------------------------------------------------------

void test_append_sched_normal(void)
{
    char body[128];
    snprintf(body, sizeof(body), "{\"st\":\"idle\"}");
    size_t n = strlen(body);
    n = auAppendSched(body, sizeof(body), n, "Planned install: Sun 8 Nov 03:00.");
    TEST_ASSERT_EQUAL_STRING("{\"st\":\"idle\",\"sched\":\"Planned install: Sun 8 Nov 03:00.\"}", body);
    TEST_ASSERT_EQUAL_UINT(strlen(body), n);
    // an empty and a missing notice are still sent (the page clears its line)
    snprintf(body, sizeof(body), "{\"st\":\"idle\"}");
    n = auAppendSched(body, sizeof(body), strlen(body), "");
    TEST_ASSERT_EQUAL_STRING("{\"st\":\"idle\",\"sched\":\"\"}", body);
    snprintf(body, sizeof(body), "{\"st\":\"idle\"}");
    n = auAppendSched(body, sizeof(body), strlen(body), nullptr);
    TEST_ASSERT_EQUAL_STRING("{\"st\":\"idle\",\"sched\":\"\"}", body);
    // an empty object gets no leading comma
    snprintf(body, sizeof(body), "{}");
    n = auAppendSched(body, sizeof(body), 2, "x");
    TEST_ASSERT_EQUAL_STRING("{\"sched\":\"x\"}", body);
    TEST_ASSERT_EQUAL_UINT(strlen(body), n);
}

void test_append_sched_overflow_leaves_body_intact(void)
{
    const char *base = "{\"st\":\"idle\"}";
    const char *want = "{\"st\":\"idle\",\"sched\":\"abcdef\"}";
    const size_t need = strlen(want) + 1; // with the NUL
    char body[128];
    // exact fit works, one byte less leaves the body as it was
    for (size_t cap = strlen(base) + 1; cap <= need + 2; cap++)
    {
        memset(body, 'Z', sizeof(body));
        snprintf(body, sizeof(body), "%s", base);
        const size_t n0 = strlen(body);
        const size_t n = auAppendSched(body, cap, n0, "abcdef");
        if (cap >= need)
        {
            TEST_ASSERT_EQUAL_STRING(want, body);
            TEST_ASSERT_EQUAL_UINT(strlen(want), n);
        }
        else
        {
            TEST_ASSERT_EQUAL_STRING_MESSAGE(base, body, "body damaged on overflow");
            TEST_ASSERT_EQUAL_UINT(n0, n);
        }
    }
    // a body that is not a closed object (or n out of range) is returned unchanged
    snprintf(body, sizeof(body), "[1]x");
    TEST_ASSERT_EQUAL_UINT(4, auAppendSched(body, sizeof(body), 4, "a"));
    TEST_ASSERT_EQUAL_STRING("[1]x", body);
    TEST_ASSERT_EQUAL_UINT(0, auAppendSched(body, sizeof(body), 0, "a"));
    TEST_ASSERT_EQUAL_UINT(1, auAppendSched(body, sizeof(body), 1, "a"));
    snprintf(body, sizeof(body), "{}");
    TEST_ASSERT_EQUAL_UINT(2, auAppendSched(body, 2, 2, "a")); // n >= cap
    TEST_ASSERT_EQUAL_UINT(2, auAppendSched(nullptr, 8, 2, "a"));
}

void test_append_sched_escapes_quotes_and_control_characters(void)
{
    char body[200];
    snprintf(body, sizeof(body), "{\"st\":\"idle\"}");
    const char evil[] = {'a', '"', 'b', '\\', 'c', '\n', '\r', '\t', 0x01, 0x1f, 0x7f, '<', '/', 'z', 0};
    const size_t n = auAppendSched(body, sizeof(body), strlen(body), evil);
    TEST_ASSERT_EQUAL_STRING("{\"st\":\"idle\",\"sched\":\"a\\\"b\\\\c\\u000a\\u000d\\u0009\\u0001\\u001f\\u007f</z\"}", body);
    TEST_ASSERT_EQUAL_UINT(strlen(body), n);
    // no raw control character and no unescaped quote inside the value
    const char *v = strstr(body, "\"sched\":\"") + 9;
    for (const char *p = v; *p && !(*p == '"' && p[-1] != '\\'); p++)
        TEST_ASSERT_TRUE((unsigned char)*p >= 0x20 && (unsigned char)*p != 0x7f);
    // a notice-shaped text with quotes at the very end of the buffer still ends in a valid object
    char tight[64];
    snprintf(tight, sizeof(tight), "{\"a\":1}");
    const size_t n2 = auAppendSched(tight, sizeof(tight), strlen(tight), "\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"\"");
    TEST_ASSERT_EQUAL_UINT(strlen(tight), n2);
    TEST_ASSERT_EQUAL_CHAR('}', tight[n2 - 1]);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", tight); // 40 escaped quotes (80 B) do not fit 64: unchanged
}

// ---- parsers -----------------------------------------------------------------------------------------

void test_parse_delay(void)
{
    int v = -1;
    for (int d = 0; d <= 7; d++)
    {
        char s[2] = {(char)('0' + d), 0};
        TEST_ASSERT_TRUE(fwSchedParseDelay(s, &v));
        TEST_ASSERT_EQUAL_INT(d, v);
    }
    v = 55;
    TEST_ASSERT_FALSE(fwSchedParseDelay("8", &v));
    TEST_ASSERT_FALSE(fwSchedParseDelay("-1", &v));
    TEST_ASSERT_FALSE(fwSchedParseDelay("", &v));
    TEST_ASSERT_FALSE(fwSchedParseDelay("10", &v));
    TEST_ASSERT_FALSE(fwSchedParseDelay("3x", &v));
    TEST_ASSERT_FALSE(fwSchedParseDelay("x", &v));
    TEST_ASSERT_FALSE(fwSchedParseDelay(nullptr, &v));
    TEST_ASSERT_EQUAL_INT(55, v);
}

void test_parse_day(void)
{
    int v = -1;
    TEST_ASSERT_TRUE(fwSchedParseDay("off", &v));
    TEST_ASSERT_EQUAL_INT(0, v);
    const char *n[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
    for (int i = 0; i < 7; i++)
    {
        TEST_ASSERT_TRUE(fwSchedParseDay(n[i], &v));
        TEST_ASSERT_EQUAL_INT(i + 1, v);
    }
    TEST_ASSERT_TRUE(fwSchedParseDay("SUN", &v));
    TEST_ASSERT_EQUAL_INT(7, v);
    TEST_ASSERT_TRUE(fwSchedParseDay("Fri", &v));
    TEST_ASSERT_EQUAL_INT(5, v);
    v = 55;
    TEST_ASSERT_FALSE(fwSchedParseDay("monday", &v));
    TEST_ASSERT_FALSE(fwSchedParseDay("mo", &v));
    TEST_ASSERT_FALSE(fwSchedParseDay("none", &v));
    TEST_ASSERT_FALSE(fwSchedParseDay("xyz", &v));
    TEST_ASSERT_FALSE(fwSchedParseDay("", &v));
    TEST_ASSERT_FALSE(fwSchedParseDay(nullptr, &v));
    TEST_ASSERT_EQUAL_INT(55, v);
}

void test_parse_time(void)
{
    int v = -1;
    TEST_ASSERT_TRUE(fwSchedParseTime("03:00", &v));
    TEST_ASSERT_EQUAL_INT(180, v);
    TEST_ASSERT_TRUE(fwSchedParseTime("3:05", &v));
    TEST_ASSERT_EQUAL_INT(185, v);
    TEST_ASSERT_TRUE(fwSchedParseTime("00:00", &v));
    TEST_ASSERT_EQUAL_INT(0, v);
    TEST_ASSERT_TRUE(fwSchedParseTime("23:59", &v));
    TEST_ASSERT_EQUAL_INT(1439, v);
    v = 55;
    TEST_ASSERT_FALSE(fwSchedParseTime("24:00", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime("12:60", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime("1200", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime("12:5", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime("12:005", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime("123:00", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime(":30", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime("ab:cd", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime("", &v));
    TEST_ASSERT_FALSE(fwSchedParseTime(nullptr, &v));
    TEST_ASSERT_EQUAL_INT(55, v);
}

// ---- the schedule cannot start an install ------------------------------------------------------------

static std::string readRepoFile(const char *rel)
{
    const std::string self = "test/test_au_schedule/test_main.cpp";
    char cwdBuf[4096];
    TEST_ASSERT_NOT_NULL_MESSAGE(getcwd(cwdBuf, sizeof(cwdBuf)), "getcwd() failed");
    std::string dir(cwdBuf);
    for (int hops = 0; hops < 16; hops++)
    {
        std::ifstream a((dir + "/" + self).c_str()), b((dir + "/platformio.ini").c_str());
        if (a.good() && b.good())
            break;
        const size_t pos = dir.find_last_of('/');
        if (pos == std::string::npos || pos == 0)
            TEST_FAIL_MESSAGE("could not derive repo root");
        dir = dir.substr(0, pos);
    }
    std::ifstream f((dir + "/" + rel).c_str(), std::ios::binary);
    TEST_ASSERT_TRUE_MESSAGE(f.good(), (std::string("could not open ") + rel).c_str());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void test_auto_install_guard_still_in_place(void)
{
    const std::string h = readRepoFile("src/fw_update.h");
    TEST_ASSERT_TRUE(h.find("#if defined(AU_AUTO_INSTALL) && (AU_AUTO_INSTALL + 0) != 0\n#error \"AU_AUTO_INSTALL must stay 0") !=
                     std::string::npos);
    TEST_ASSERT_TRUE(h.find("#undef AU_AUTO_INSTALL\n#define AU_AUTO_INSTALL 0") != std::string::npos);
    TEST_ASSERT_TRUE(h.find("#define AU_AUTO_INSTALL 1") == std::string::npos);
    // the compile-time proof: with the include above, the macro is 0 in this translation unit too
    // (fw_update.h is not included here; the text check is the pin)
}

void test_schedule_module_cannot_start_an_install(void)
{
    const std::string s = readRepoFile("src/fw_schedule.h");
    // pure header: only libc headers, nothing that can download, flash or reboot
    const char *banned[] = {"fwNetStart", "esp_restart", "ESP.restart", "Update.", "fwApply", "auRequestApply",
                            "WiFiClient", "HTTPClient", "esp_ota", "Serial", "#include \"fw_update", "AU_AUTO_INSTALL 1"};
    for (size_t i = 0; i < sizeof(banned) / sizeof(banned[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(s.find(banned[i]) == std::string::npos, banned[i]);
    // it does not even define AU_AUTO_INSTALL
    TEST_ASSERT_TRUE(s.find("#define AU_AUTO_INSTALL") == std::string::npos);
    // the due branch of the caller: logs and nothing else, and sits outside #if AU_AUTO_INSTALL
    const std::string n = readRepoFile("src/esp32/fw_update_net.cpp");
    const size_t at = n.find("[AU];sched;due;blocked;auto_install_off");
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, "due log line missing");
    const size_t end = n.find("\n}\n", at);
    TEST_ASSERT_TRUE(end != std::string::npos);
    const std::string due = n.substr(at, end - at);
    TEST_ASSERT_TRUE(due.find("fwNetStart") == std::string::npos && due.find("startJob") == std::string::npos &&
                     due.find("restart") == std::string::npos);

    // the whole schedule section starts no job, and the simulated candidate never leaves it: s_simOn / s_simRel
    // are not referenced by CHECK, DOWNLOAD, STAGELAN or the status (s_st / s_av), so the Install path cannot see it
    const size_t rs = n.find("// Install schedule (src/fw_schedule.h)");
    const size_t re = n.find("bool fwNetStagedPending(void)\n{", rs);
    TEST_ASSERT_TRUE(rs != std::string::npos && re != std::string::npos && re > rs);
    const std::string sec = n.substr(rs, re - rs);
    const char *noJob[] = {"startJob(", "fwNetStart(", "esp_restart", "stageFlash", "ESP.restart", "Update.", "s_av.", "s_argJob"};
    for (size_t i = 0; i < sizeof(noJob) / sizeof(noJob[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(sec.find(noJob[i]) == std::string::npos, noJob[i]);
    const char *sims[] = {"s_simOn", "s_simRel"};
    for (size_t i = 0; i < 2; i++)
    {
        size_t pos = 0;
        int seen = 0;
        while ((pos = n.find(sims[i], pos)) != std::string::npos)
        {
            TEST_ASSERT_TRUE_MESSAGE(pos >= rs && pos < re, sims[i]);
            pos += strlen(sims[i]);
            seen++;
        }
        TEST_ASSERT_TRUE_MESSAGE(seen > 0, sims[i]);
    }
    // the schedule may read s_st (the candidate) but never write it
    TEST_ASSERT_TRUE(sec.find("s_st.availNewer =") == std::string::npos && sec.find("s_st.installable") == std::string::npos);
}

void test_schedule_reads_the_node_clock_not_the_libc_clock(void)
{
    // The libc clock is never set on this firmware (no SNTP, no settimeofday), so time(nullptr) is seconds since
    // boot. The schedule must take the node time (node_date_*) converted with node_utcoff.
    const std::string n = readRepoFile("src/esp32/fw_update_net.cpp");
    const size_t rs = n.find("// Install schedule (src/fw_schedule.h)");
    const size_t re = n.find("bool fwNetStagedPending(void)\n{", rs);
    TEST_ASSERT_TRUE(rs != std::string::npos && re != std::string::npos && re > rs);
    const std::string sec = n.substr(rs, re - rs);
    const char *clocks[] = {"time(nullptr)", "time(NULL)", "time(0)", "time(&", "gettimeofday", "millis()", "esp_timer", "localtime", "mktime", "getUnixClock"};
    for (size_t i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(sec.find(clocks[i]) == std::string::npos, clocks[i]);
    TEST_ASSERT_TRUE_MESSAGE(sec.find("rtcUtcFromLocalFields(meshcom_settings.node_date_year") != std::string::npos, "schedule clock is not the node time");
    TEST_ASSERT_TRUE_MESSAGE(sec.find("node_date_year > 2023") != std::string::npos, "no year plausibility gate");
    TEST_ASSERT_TRUE_MESSAGE(sec.find("meshcom_settings.node_utcoff") != std::string::npos, "node_utcoff is not applied");
    const std::string h = readRepoFile("src/fw_schedule.h");
    TEST_ASSERT_TRUE(h.find("time(nullptr)") == std::string::npos && h.find("time(NULL)") == std::string::npos);
    // the notice marks a bench clock jump as well as a simulated candidate
    TEST_ASSERT_TRUE_MESSAGE(sec.find("v.simulated || v.jumped") != std::string::npos, "jumped is not shown in the notice");
    // the candidate is checked before the TZ string is parsed (the tick runs every second)
    const size_t cand = sec.find("s_st.availNewer");
    const size_t tz = sec.find("tzParse(");
    TEST_ASSERT_TRUE_MESSAGE(cand != std::string::npos && tz != std::string::npos && cand < tz, "tzParse runs before the candidate check");
}

void test_no_libc_clock_anywhere_in_the_au_net_files(void)
{
    // The section scan above covers the schedule only. The same trap (time(nullptr) is seconds since boot, the libc
    // clock is never set) must not creep into the rest of the auto-update and remote-management code either.
    const char *files[] = {"src/esp32/fw_update_net.cpp", "src/esp32/rm_au_esp32.cpp"};
    const char *clocks[] = {"time(nullptr)", "time(NULL)", "time(0)", "time(&"};
    for (size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++)
    {
        const std::string src = readRepoFile(files[f]);
        TEST_ASSERT_TRUE_MESSAGE(src.size() > 1000, files[f]);   // really read, not an empty stub
        for (size_t i = 0; i < sizeof(clocks) / sizeof(clocks[0]); i++)
            TEST_ASSERT_TRUE_MESSAGE(src.find(clocks[i]) == std::string::npos, (std::string(files[f]) + ": " + clocks[i]).c_str());
    }
}

void test_console_verbs_have_their_own_errors_and_help(void)
{
    const std::string c = readRepoFile("src/command_functions.cpp");
    // "--autoupdate delay" without a value must not fall through to "must be notify or auto"
    const size_t at = c.find("auSchedCommand(const char *arg)");
    TEST_ASSERT_TRUE(at != std::string::npos);
    const std::string body = c.substr(at, 3500);
    const char *own[] = {"delay needs a value", "day needs a value", "time needs a value", "auSchedVerb(arg, \"delay\"", "auSchedVerb(arg, \"day\"", "auSchedVerb(arg, \"time\"", "\"status\""};
    for (size_t i = 0; i < sizeof(own) / sizeof(own[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(body.find(own[i]) != std::string::npos, own[i]);
    // the help lists the verbs
    const char *help[] = {"--autoupdate status ", "--autoupdate delay 0-7 ", "--autoupdate day off|mon..sun", "--autoupdate time HH:MM", "--autest"};
    for (size_t i = 0; i < 4; i++)
        TEST_ASSERT_TRUE_MESSAGE(c.find(help[i]) != std::string::npos, help[i]);
    // the two helpers stay out of the commandAction frame
    TEST_ASSERT_TRUE(c.find("static __attribute__((noinline)) void auSchedPrint(void)") != std::string::npos);
    TEST_ASSERT_TRUE(c.find("static __attribute__((noinline)) bool auSchedCommand(const char *arg)") != std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_calendar_helpers);
    RUN_TEST(test_no_candidate);
    RUN_TEST(test_clock_invalid);
    RUN_TEST(test_waiting_then_due_boundary);
    RUN_TEST(test_days_and_hours_left);
    RUN_TEST(test_delay_zero_is_next_night);
    RUN_TEST(test_release_before_install_time_same_day);
    RUN_TEST(test_release_after_install_time_same_day);
    RUN_TEST(test_release_exactly_at_install_time_counts);
    RUN_TEST(test_delay_seven);
    RUN_TEST(test_install_time_adjustable);
    RUN_TEST(test_month_rollover);
    RUN_TEST(test_year_rollover);
    RUN_TEST(test_leap_day);
    RUN_TEST(test_dst_spring_forward);
    RUN_TEST(test_dst_fall_back);
    RUN_TEST(test_dst_day_length_does_not_skip_or_repeat);
    RUN_TEST(test_plain_utc_converter);
    RUN_TEST(test_weekday_basic);
    RUN_TEST(test_weekday_same_day_only_if_still_ahead);
    RUN_TEST(test_weekday_never_later_than_seven_days);
    RUN_TEST(test_weekday_lands_on_that_weekday);
    RUN_TEST(test_invalid_inputs_are_clamped);
    RUN_TEST(test_release_from_tag_date);
    RUN_TEST(test_release_from_tag_leap_and_invalid);
    RUN_TEST(test_release_from_tag_plan_delay_three);
    RUN_TEST(test_parse_utc);
    RUN_TEST(test_notice_text_exact);
    RUN_TEST(test_notice_is_honest_in_every_state);
    RUN_TEST(test_notice_truncation_is_safe);
    RUN_TEST(test_notice_simulated_prefix);
    RUN_TEST(test_notice_worst_case_fits);
    RUN_TEST(test_sanitize_repairs_to_defaults);
    RUN_TEST(test_settings_defaults_and_schema_are_pinned);
    RUN_TEST(test_append_sched_normal);
    RUN_TEST(test_append_sched_overflow_leaves_body_intact);
    RUN_TEST(test_append_sched_escapes_quotes_and_control_characters);
    RUN_TEST(test_parse_delay);
    RUN_TEST(test_parse_day);
    RUN_TEST(test_parse_time);
    RUN_TEST(test_auto_install_guard_still_in_place);
    RUN_TEST(test_schedule_module_cannot_start_an_install);
    RUN_TEST(test_schedule_reads_the_node_clock_not_the_libc_clock);
    RUN_TEST(test_no_libc_clock_anywhere_in_the_au_net_files);
    RUN_TEST(test_console_verbs_have_their_own_errors_and_help);
    return UNITY_END();
}
