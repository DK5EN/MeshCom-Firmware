// DISP-PAGE-01 / DISP-PAGE-03 (#1294): src/page_ring.h -- click walk over the message ring and the
// Wireless Paper browse. The legacy*() functions are verbatim copies of the pre-fix firmware logic
// (src/onebutton_functions.cpp generic branch and WP_DISP branch); tests built on them are
// characterisation of that copy, NOT measurements of the firmware. The firmware wiring is pinned in
// test_display_clock (source pins).
#include <unity.h>
#include <stdio.h>
#include <string.h>

#include "page_ring.h"

void setUp(void) {}
void tearDown(void) {}

#define MAXSLOTS 16

struct Ring
{
    int lineAnz[MAXSLOTS];
    int last;
    int pageMax;
};

static void ring_init(Ring &r, int pageMax, int last)
{
    memset(&r, 0, sizeof(r));
    r.pageMax = pageMax;
    r.last = last;
}

// the way sendDisplay1306() writes it: store in slot `last`, advance, clear the next slot
static void ring_store(Ring &r)
{
    r.lineAnz[r.last] = 3;
    r.last++;
    if (r.last >= r.pageMax) r.last = 0;
    r.lineAnz[r.last] = 0;
}

static int ring_newest(const Ring &r) { return pageRingNewest(r.last, r.pageMax); }

// ring with n messages, the newest in slot k
static void ring_with(Ring &r, int pageMax, int k, int n)
{
    ring_init(r, pageMax, ((k - n + 1) % pageMax + pageMax) % pageMax);
    for (int i = 0; i < n; i++) ring_store(r);
}

// ---- legacy copies ----

struct LegacyClick { bool status; int slot; int next; };

// pre-fix generic branch (OLED/TFT/T-Echo): stop at the absolute slot 5
static LegacyClick legacyClick(const Ring &r, int pointer)
{
    LegacyClick c = { false, -1, pointer };
    if (pointer == 5) { c.status = true; return c; }
    if (r.lineAnz[pointer] == 0) pointer = r.last - 1;   // -1 at boot: the out-of-bounds index
    c.slot = pointer;
    pointer--;
    if (pointer < 0) pointer = r.pageMax - 1;
    c.next = pointer;
    return c;
}

// pre-fix E290 block, verbatim (oracle for the equivalence test)
static bool legacyE290(const Ring &r, int pointer, int &slotOut, int &pageNoOut, int &nextOut)
{
    int e290Newest = r.last - 1;
    if (e290Newest < 0) e290Newest += r.pageMax;
    if (r.lineAnz[pointer] == 0) { nextOut = e290Newest; return false; }
    int e290Count = 0, e290Slot = e290Newest;
    for (int n = 0; n < r.pageMax; n++)
    {
        if (r.lineAnz[e290Slot] == 0) break;
        e290Count++;
        e290Slot--; if (e290Slot < 0) e290Slot += r.pageMax;
    }
    pageNoOut = e290Count - ((e290Newest - pointer + r.pageMax) % r.pageMax);
    slotOut = pointer;
    nextOut = pointer - 1;
    if (nextOut < 0) nextOut = r.pageMax - 1;
    return true;
}

struct LegacyWp { bool status; int slot; int rank; bool statusStep; int pointer; };

// pre-fix WP_DISP branch, verbatim
static LegacyWp legacyWpClick(const Ring &r, int browseMax, bool statusStep, int pointer)
{
    const int PM = r.pageMax;
    LegacyWp o = { true, -1, 0, false, 0 };
    int wpNewest = r.last - 1;
    if (wpNewest < 0) wpNewest += PM;
    if (r.lineAnz[wpNewest] == 0) { o.pointer = wpNewest; return o; }
    if (statusStep) { o.pointer = wpNewest; return o; }
    if (r.lineAnz[pointer] == 0) pointer = wpNewest;
    int wpN = 0;
    {
        int s = wpNewest;
        for (int n = 0; n < PM; n++)
        {
            if (r.lineAnz[s] == 0) break;
            wpN++;
            s--; if (s < 0) s += PM;
        }
    }
    int wpShow = (wpN < browseMax) ? wpN : browseMax;
    int wpSteps = (wpNewest - pointer + PM) % PM;
    if (wpSteps >= wpShow) { pointer = wpNewest; wpSteps = 0; }
    int wpRank = wpShow - wpSteps;
    o.status = false;
    o.slot = pointer;
    o.rank = wpRank;
    int wpNext = pointer - 1;
    if (wpNext < 0) wpNext += PM;
    if ((wpSteps + 1) >= wpShow || r.lineAnz[wpNext] == 0) o.statusStep = true;
    else pointer = wpNext;
    o.pointer = pointer;
    return o;
}

// ---- DISP-PAGE-01 ----

// click from pointer = newest until Status; returns the number of messages visited, fills slots/ranks
static int walk(const Ring &r, int slots[], int ranks[])
{
    int pointer = ring_newest(r), visited = 0;
    for (int guard = 0; guard < 2 * MAXSLOTS; guard++)
    {
        PageClickStep s = pageClickStep(r.lineAnz, r.pageMax, r.last, pointer);
        if (s.kind == PageClickKind::Status) return visited;
        slots[visited] = s.slot;
        ranks[visited] = s.rank;
        visited++;
        pointer = s.nextPointer;
    }
    TEST_FAIL_MESSAGE("click walk does not end");
    return -1;
}

void test_depth_is_stored_count_for_every_ring_position(void)
{
    for (int k = 0; k < 6; k++)
        for (int n = 1; n <= 5; n++)
        {
            Ring r;
            ring_with(r, 6, k, n);
            int slots[MAXSLOTS], ranks[MAXSLOTS];
            const int visited = walk(r, slots, ranks);
            char msg[64];
            snprintf(msg, sizeof(msg), "newest slot %d, stored %d", k, n);
            TEST_ASSERT_EQUAL_INT_MESSAGE(n, visited, msg);
            for (int i = 0; i < n; i++)   // newest to oldest, rank n..1
            {
                TEST_ASSERT_EQUAL_INT_MESSAGE(((k - i) % 6 + 6) % 6, slots[i], msg);
                TEST_ASSERT_EQUAL_INT_MESSAGE(n - i, ranks[i], msg);
            }
        }
}

void test_wrapped_ring_shows_five_of_many(void)
{
    Ring r;
    ring_init(r, 6, 0);
    for (int i = 0; i < 23; i++) ring_store(r);
    int slots[MAXSLOTS], ranks[MAXSLOTS];
    TEST_ASSERT_EQUAL_INT(5, walk(r, slots, ranks));
    TEST_ASSERT_EQUAL_INT(5, ranks[0]);
    TEST_ASSERT_EQUAL_INT(1, ranks[4]);
}

void test_status_page_after_oldest_and_loop(void)
{
    Ring r;
    ring_with(r, 6, 3, 2);   // slots 3 (newest), 2
    PageClickStep s = pageClickStep(r.lineAnz, 6, r.last, 3);
    TEST_ASSERT_TRUE(s.kind == PageClickKind::Message);
    s = pageClickStep(r.lineAnz, 6, r.last, s.nextPointer);
    TEST_ASSERT_TRUE(s.kind == PageClickKind::Message);
    TEST_ASSERT_EQUAL_INT(2, s.slot);
    s = pageClickStep(r.lineAnz, 6, r.last, s.nextPointer);   // past the oldest
    TEST_ASSERT_TRUE(s.kind == PageClickKind::Status);
    TEST_ASSERT_EQUAL_INT(3, s.nextPointer);
    s = pageClickStep(r.lineAnz, 6, r.last, s.nextPointer);   // the newest again
    TEST_ASSERT_TRUE(s.kind == PageClickKind::Message);
    TEST_ASSERT_EQUAL_INT(3, s.slot);
    TEST_ASSERT_EQUAL_INT(2, s.rank);
}

void test_empty_ring_and_boot_pointer(void)
{
    // boot: pageLastPointer = 0, pagePointer = 0, slot 0 empty. The legacy copy indexes slot -1.
    Ring r;
    ring_init(r, 6, 0);
    PageClickStep s = pageClickStep(r.lineAnz, 6, 0, 0);
    TEST_ASSERT_TRUE(s.kind == PageClickKind::Status);
    TEST_ASSERT_EQUAL_INT(-1, s.slot);
    TEST_ASSERT_EQUAL_INT(5, s.nextPointer);   // newest = last-1 wrapped; never an index below 0
    TEST_ASSERT_TRUE(s.nextPointer >= 0);
    for (int p = -2; p < 8; p++)               // any pointer, in or out of range, is Status
        TEST_ASSERT_TRUE(pageClickStep(r.lineAnz, 6, 0, p).kind == PageClickKind::Status);
    TEST_ASSERT_EQUAL_INT(-1, legacyClick(r, 0).slot);   // characterisation of the legacy copy
}

void test_out_of_range_pointer_is_status_never_an_index(void)
{
    Ring r;
    ring_with(r, 6, 2, 3);
    TEST_ASSERT_TRUE(pageClickStep(r.lineAnz, 6, r.last, -1).kind == PageClickKind::Status);
    TEST_ASSERT_TRUE(pageClickStep(r.lineAnz, 6, r.last, 6).kind == PageClickKind::Status);
    TEST_ASSERT_TRUE(pageClickStep(r.lineAnz, 6, r.last, 99).kind == PageClickKind::Status);
}

void test_e290_equivalence(void)
{
    const int PM = 10;
    for (int last = 0; last < PM; last++)
        for (int fill = 0; fill < PM; fill++)   // fill = stored messages, 0..9
            for (int pointer = 0; pointer < PM; pointer++)
            {
                Ring r;
                ring_init(r, PM, ((last - fill) % PM + PM) % PM);
                for (int i = 0; i < fill; i++) ring_store(r);
                TEST_ASSERT_EQUAL_INT(last, r.last);
                int slot = -1, pageNo = 0, next = -1;
                const bool msg = legacyE290(r, pointer, slot, pageNo, next);
                const PageClickStep s = pageClickStep(r.lineAnz, PM, r.last, pointer);
                if (msg)
                {
                    TEST_ASSERT_TRUE(s.kind == PageClickKind::Message);
                    TEST_ASSERT_EQUAL_INT(slot, s.slot);
                    TEST_ASSERT_EQUAL_INT(pageNo, s.rank);
                    TEST_ASSERT_EQUAL_INT(next, s.nextPointer);
                }
                else
                {
                    TEST_ASSERT_TRUE(s.kind == PageClickKind::Status);
                    TEST_ASSERT_EQUAL_INT(next, s.nextPointer);
                }
            }
}

void test_legacy_copy_depth_documents_the_defect(void)
{
    // characterisation of the legacy copy only: k = 2, n = 5 visits 3 slots, k = 5 none
    Ring r;
    ring_with(r, 6, 2, 5);
    int pointer = 2, visited = 0;
    for (int g = 0; g < 20; g++)
    {
        const LegacyClick c = legacyClick(r, pointer);
        if (c.status) break;
        visited++;
        pointer = c.next;
    }
    TEST_ASSERT_EQUAL_INT(3, visited);
    ring_with(r, 6, 5, 5);
    TEST_ASSERT_TRUE(legacyClick(r, 5).status);
}

// ---- DISP-PAGE-03 ----

static const int WPMAX = 10, WPBROWSE = 4;

static WpBrowseResult wpClick(const Ring &r, const WpBrowseState &st)
{
    return wpBrowseClick(r.lineAnz, r.pageMax, r.last, WPBROWSE, st);
}

void test_first_click_after_arrival_shows_second_newest(void)
{
    Ring r;
    ring_with(r, WPMAX, 7, 3);   // slots 5, 6, 7; newest 7
    WpBrowseState st = { false, true, 7 };
    // the legacy copy redraws the newest with rank 3
    const LegacyWp lg = legacyWpClick(r, WPBROWSE, false, 7);
    TEST_ASSERT_EQUAL_INT(7, lg.slot);
    TEST_ASSERT_EQUAL_INT(3, lg.rank);

    WpBrowseResult a = wpClick(r, st);
    TEST_ASSERT_FALSE(a.status);
    TEST_ASSERT_EQUAL_INT(6, a.slot);
    TEST_ASSERT_EQUAL_INT(2, a.rank);
    TEST_ASSERT_FALSE(a.next.liveNewest);
    WpBrowseResult b = wpClick(r, a.next);
    TEST_ASSERT_FALSE(b.status);
    TEST_ASSERT_EQUAL_INT(5, b.slot);
    TEST_ASSERT_EQUAL_INT(1, b.rank);
    WpBrowseResult c = wpClick(r, b.next);
    TEST_ASSERT_TRUE(c.status);
    WpBrowseResult d = wpClick(r, c.next);
    TEST_ASSERT_FALSE(d.status);
    TEST_ASSERT_EQUAL_INT(7, d.slot);
    TEST_ASSERT_EQUAL_INT(3, d.rank);   // not live any more
}

void test_single_message_arrival_click_goes_to_status(void)
{
    Ring r;
    ring_with(r, WPMAX, 0, 1);
    WpBrowseState st = { false, true, 0 };
    TEST_ASSERT_FALSE(legacyWpClick(r, WPBROWSE, false, 0).status);   // legacy redraws the message
    TEST_ASSERT_TRUE(wpClick(r, st).status);
    TEST_ASSERT_FALSE(wpClick(r, st).next.liveNewest);
}

void test_fresh_arrival_supersedes_pending_status_step(void)
{
    Ring r;
    ring_with(r, WPMAX, 4, 3);
    WpBrowseState st = { true, true, 4 };   // status step left over from the last walk, then an arrival
    const WpBrowseResult a = wpClick(r, st);
    TEST_ASSERT_FALSE(a.status);
    TEST_ASSERT_EQUAL_INT(3, a.slot);
    TEST_ASSERT_EQUAL_INT(2, a.rank);
}

void test_not_live_equals_legacy(void)
{
    for (int last = 0; last < WPMAX; last++)
        for (int fill = 0; fill < WPMAX; fill++)
            for (int pointer = 0; pointer < WPMAX; pointer++)
                for (int ss = 0; ss < 2; ss++)
                {
                    Ring r;
                    ring_init(r, WPMAX, ((last - fill) % WPMAX + WPMAX) % WPMAX);
                    for (int i = 0; i < fill; i++) ring_store(r);
                    const LegacyWp o = legacyWpClick(r, WPBROWSE, ss != 0, pointer);
                    const WpBrowseState st = { ss != 0, false, pointer };
                    const WpBrowseResult n = wpClick(r, st);
                    TEST_ASSERT_EQUAL_INT(o.status ? 1 : 0, n.status ? 1 : 0);
                    TEST_ASSERT_EQUAL_INT(o.statusStep ? 1 : 0, n.next.statusStep ? 1 : 0);
                    TEST_ASSERT_EQUAL_INT(o.pointer, n.next.pointer);
                    TEST_ASSERT_FALSE(n.next.liveNewest);
                    if (!o.status)
                    {
                        TEST_ASSERT_EQUAL_INT(o.slot, n.slot);
                        TEST_ASSERT_EQUAL_INT(o.rank, n.rank);
                    }
                }
}

void test_rank_on_arrival(void)
{
    for (int count = 1; count <= 9; count++)
        TEST_ASSERT_EQUAL_INT(count < WPBROWSE ? count : WPBROWSE, wpBrowseShow(count, WPBROWSE));
    // the live counter and the rank of the first click walk agree: newest = wpBrowseShow()
    for (int fill = 1; fill <= 9; fill++)
    {
        Ring r;
        ring_with(r, WPMAX, 3, fill);
        const WpBrowseState st = { false, false, 3 };   // browse from the newest, not live
        TEST_ASSERT_EQUAL_INT(wpBrowseShow(pageRingCount(r.lineAnz, WPMAX, r.last), WPBROWSE), wpClick(r, st).rank);
    }
}

// T114-DARK-01 (#1314, --msgwake off): a message stored while the display stays dark is not on the panel
// (liveNewest false), but its arrival must still cancel a status step left over from the last walk, so the
// first click shows that message (newest, full rank) and not the status page.
static void test_suppressed_arrival_cancels_status_step_and_shows_newest(void)
{
    int lineAnz[WPMAX] = {0};
    for (int i = 0; i < 5; i++) lineAnz[i] = 2;
    const int last = 5;                              // newest slot = 4
    WpBrowseState st = { true, false, 4, true };     // pending status step, suppressed arrival
    const WpBrowseResult r = wpBrowseClick(lineAnz, WPMAX, last, WPBROWSE, st);
    TEST_ASSERT_FALSE_MESSAGE(r.status, "first click after a suppressed arrival went to the status page");
    TEST_ASSERT_EQUAL_INT(4, r.slot);
    TEST_ASSERT_EQUAL_INT(wpBrowseShow(5, WPBROWSE), r.rank);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_depth_is_stored_count_for_every_ring_position);
    RUN_TEST(test_wrapped_ring_shows_five_of_many);
    RUN_TEST(test_status_page_after_oldest_and_loop);
    RUN_TEST(test_empty_ring_and_boot_pointer);
    RUN_TEST(test_out_of_range_pointer_is_status_never_an_index);
    RUN_TEST(test_e290_equivalence);
    RUN_TEST(test_legacy_copy_depth_documents_the_defect);
    RUN_TEST(test_first_click_after_arrival_shows_second_newest);
    RUN_TEST(test_single_message_arrival_click_goes_to_status);
    RUN_TEST(test_fresh_arrival_supersedes_pending_status_step);
    RUN_TEST(test_not_live_equals_legacy);
    RUN_TEST(test_rank_on_arrival);
    RUN_TEST(test_suppressed_arrival_cancels_status_step_and_shows_newest);
    return UNITY_END();
}
