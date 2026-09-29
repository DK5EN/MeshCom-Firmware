// Native suite for loop_breadcrumb (F3, docs/soak-20260928-verdict.md Finding 3):
// record/validate/format/clear logic of the always-on LAST_LOOP_SECTION
// breadcrumb. On native the RTC_NOINIT attribute is a no-op and LOOP_SECTION()
// is a no-op macro, so the tests drive LoopSectionGuard directly.
//
//   pio test -e native -f test_loop_breadcrumb

#include <unity.h>

#include <stdint.h>
#include <string.h>
#include <new>

#include <loop_breadcrumb.h>

void setUp(void) { loopCrumbClear(); }
void tearDown(void) {}

static LoopCrumb snap(void)
{
    LoopCrumb c;
    c.tag = g_loopCrumb.tag;
    c.ms  = g_loopCrumb.ms;
    c.chk = g_loopCrumb.chk;
    return c;
}

static void test_cleared_record_is_invalid_and_silent(void)
{
    char buf[80];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_FALSE(loopCrumbValid(snap(), NULL));
    TEST_ASSERT_FALSE(loopCrumbTakeReport(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_random_garbage_is_invalid(void)
{
    // What RTC_NOINIT memory looks like after a power cycle: arbitrary words.
    uint32_t w[3] = { 0xDEADBEEFu, 0x12345678u, 0xCAFEBABEu };
    LoopCrumb c = { w[0], w[1], w[2] };
    TEST_ASSERT_FALSE(loopCrumbValid(c, NULL));

    // Right tag high half, wrong inverse byte.
    c = loopCrumbMake(LSEC_WEB, 100);
    c.tag ^= 0x1u;
    TEST_ASSERT_FALSE(loopCrumbValid(c, NULL));

    // Good tag, corrupted millis word.
    c = loopCrumbMake(LSEC_WEB, 100);
    c.ms ^= 0x80u;
    TEST_ASSERT_FALSE(loopCrumbValid(c, NULL));

    // Well-formed but id out of range.
    c = loopCrumbMake((uint8_t)LSEC_COUNT, 100);
    TEST_ASSERT_FALSE(loopCrumbValid(c, NULL));
    c = loopCrumbMake(LSEC_NONE, 100);
    TEST_ASSERT_FALSE(loopCrumbValid(c, NULL));
}

static void test_make_valid_roundtrip_all_ids(void)
{
    for(uint8_t id = LSEC_NONE + 1; id < LSEC_COUNT; id++)
    {
        uint8_t got = 0;
        LoopCrumb c = loopCrumbMake(id, 4242u + id);
        TEST_ASSERT_TRUE(loopCrumbValid(c, &got));
        TEST_ASSERT_EQUAL_UINT8(id, got);
        TEST_ASSERT_NOT_NULL(loopSectionName(id));
    }
    TEST_ASSERT_NULL(loopSectionName(LSEC_NONE));
    TEST_ASSERT_NULL(loopSectionName(LSEC_COUNT));
    TEST_ASSERT_EQUAL_STRING("web", loopSectionName(LSEC_WEB));
}

static void test_format_exact_line(void)
{
    char buf[80];
    LoopCrumb c = loopCrumbMake(LSEC_WEB, 42150u);
    int len = loopCrumbFormat(buf, sizeof(buf), c);
    TEST_ASSERT_EQUAL_STRING("[BOOT] LAST_LOOP_SECTION=web entered_ms=42150", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), len);

    // millis near the 32-bit limit prints unsigned.
    c = loopCrumbMake(LSEC_LORA_RX, 0xFFFFFFF0u);
    loopCrumbFormat(buf, sizeof(buf), c);
    TEST_ASSERT_EQUAL_STRING("[BOOT] LAST_LOOP_SECTION=lora_rx entered_ms=4294967280", buf);
}

static void test_format_small_buffer_is_safe(void)
{
    char buf[10];
    memset(buf, 'x', sizeof(buf));
    LoopCrumb c = loopCrumbMake(LSEC_WEB, 1u);
    TEST_ASSERT_EQUAL_INT(0, loopCrumbFormat(buf, sizeof(buf), c));
    TEST_ASSERT_EQUAL_STRING("", buf);
    TEST_ASSERT_EQUAL_INT(0, loopCrumbFormat(NULL, 5, c));
    TEST_ASSERT_EQUAL_INT(0, loopCrumbFormat(buf, 0, c));
}

static void test_guard_writes_on_entry_and_clears_on_exit(void)
{
    uint8_t id = 0;
    {
        LoopSectionGuard g(LSEC_GPS, 777u);
        LoopCrumb c = snap();
        TEST_ASSERT_TRUE(loopCrumbValid(c, &id));
        TEST_ASSERT_EQUAL_UINT8(LSEC_GPS, id);
        TEST_ASSERT_EQUAL_UINT32(777u, c.ms);
    }
    // Clean exit: back to the cleared state, nothing to report at next boot.
    TEST_ASSERT_FALSE(loopCrumbValid(snap(), NULL));
    char buf[80];
    TEST_ASSERT_FALSE(loopCrumbTakeReport(buf, sizeof(buf)));
}

static void test_nested_guard_restores_outer(void)
{
    uint8_t id = 0;
    {
        LoopSectionGuard outer(LSEC_LORA_RX, 100u);
        {
            LoopSectionGuard inner(LSEC_DISPLAY, 150u);
            TEST_ASSERT_TRUE(loopCrumbValid(snap(), &id));
            TEST_ASSERT_EQUAL_UINT8(LSEC_DISPLAY, id);
        }
        LoopCrumb c = snap();
        TEST_ASSERT_TRUE(loopCrumbValid(c, &id));
        TEST_ASSERT_EQUAL_UINT8(LSEC_LORA_RX, id);
        TEST_ASSERT_EQUAL_UINT32(100u, c.ms);
    }
    TEST_ASSERT_FALSE(loopCrumbValid(snap(), NULL));
}

static void test_reset_inside_section_is_reported_then_cleared(void)
{
    // Simulate the chip resetting while the guard is live: the destructor
    // never runs, the record survives into the "next boot".
    char buf[80];
    alignas(LoopSectionGuard) unsigned char storage[sizeof(LoopSectionGuard)];
    new (storage) LoopSectionGuard(LSEC_WEB, 34900u);   // deliberately never destroyed

    TEST_ASSERT_TRUE(loopCrumbTakeReport(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("[BOOT] LAST_LOOP_SECTION=web entered_ms=34900", buf);

    // Reading clears: a second boot without a new stall prints nothing.
    TEST_ASSERT_FALSE(loopCrumbTakeReport(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

static void test_take_report_clears_garbage_too(void)
{
    g_loopCrumb.tag = 0xDEADBEEFu;
    g_loopCrumb.ms  = 1u;
    g_loopCrumb.chk = 2u;
    char buf[80];
    TEST_ASSERT_FALSE(loopCrumbTakeReport(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT32(0u, g_loopCrumb.tag);
    TEST_ASSERT_EQUAL_UINT32(0u, g_loopCrumb.ms);
    TEST_ASSERT_EQUAL_UINT32(0u, g_loopCrumb.chk);
}

// INS-05: a DELIBERATE reboot inside a section (--reboot over BLE/web, web config
// import, --cleanflash, --ota-update) calls loopCrumbClear() right before the
// restart, so the next boot reports no section. Contract the call sites rely on:
// clear while a guard is live (the guard's destructor never runs, the chip
// resets) wipes the record, nested or not, and the next take is silent. Without
// the call the same reset is reported (test_reset_inside_section_is_reported_...).
static void test_deliberate_reboot_inside_section_clears_the_report(void)
{
    char buf[80];

    {
        LoopSectionGuard outer(LSEC_WEB, 1000u);
        LoopSectionGuard inner(LSEC_BLE_CMD, 2000u);
        TEST_ASSERT_TRUE(loopCrumbValid(snap(), NULL));

        loopCrumbClear();   // the call before ESP.restart()

        // "reset" here: nothing runs the guards' destructors on a real restart,
        // so read the record as the next boot would.
        TEST_ASSERT_FALSE(loopCrumbValid(snap(), NULL));
        TEST_ASSERT_FALSE(loopCrumbTakeReport(buf, sizeof(buf)));
        TEST_ASSERT_EQUAL_STRING("", buf);
        TEST_ASSERT_EQUAL_UINT32(0u, g_loopCrumb.tag);
        TEST_ASSERT_EQUAL_UINT32(0u, g_loopCrumb.ms);
        TEST_ASSERT_EQUAL_UINT32(0u, g_loopCrumb.chk);
    }

    // Control: the same reset without the clear names the innermost section.
    {
        LoopSectionGuard sec(LSEC_WEB, 3000u);
        char line[80];
        TEST_ASSERT_TRUE(loopCrumbTakeReport(line, sizeof(line)));
        TEST_ASSERT_EQUAL_STRING("[BOOT] LAST_LOOP_SECTION=web entered_ms=3000", line);
    }
}

// --info ...BOOT line: the summary starts empty, keeps what esp32setup() stored,
// truncates safely, and NULL resets it.
static void test_boot_summary_set_get_truncate(void)
{
    loopCrumbSetBootSummary(NULL);
    TEST_ASSERT_EQUAL_STRING("", loopCrumbBootSummary());

    loopCrumbSetBootSummary("RESET_REASON=6 TASK_WDT LAST_LOOP_SECTION=web entered_ms=37012");
    TEST_ASSERT_EQUAL_STRING("RESET_REASON=6 TASK_WDT LAST_LOOP_SECTION=web entered_ms=37012",
                             loopCrumbBootSummary());

    char longer[LOOPCRUMB_SUMMARY_LEN + 40];
    memset(longer, 'x', sizeof(longer) - 1);
    longer[sizeof(longer) - 1] = 0;
    loopCrumbSetBootSummary(longer);
    TEST_ASSERT_EQUAL_size_t(LOOPCRUMB_SUMMARY_LEN - 1, strlen(loopCrumbBootSummary()));

    loopCrumbSetBootSummary(NULL);
    TEST_ASSERT_EQUAL_STRING("", loopCrumbBootSummary());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_cleared_record_is_invalid_and_silent);
    RUN_TEST(test_random_garbage_is_invalid);
    RUN_TEST(test_make_valid_roundtrip_all_ids);
    RUN_TEST(test_format_exact_line);
    RUN_TEST(test_format_small_buffer_is_safe);
    RUN_TEST(test_guard_writes_on_entry_and_clears_on_exit);
    RUN_TEST(test_nested_guard_restores_outer);
    RUN_TEST(test_reset_inside_section_is_reported_then_cleared);
    RUN_TEST(test_take_report_clears_garbage_too);
    RUN_TEST(test_deliberate_reboot_inside_section_clears_the_report);
    RUN_TEST(test_boot_summary_set_get_truncate);
    return UNITY_END();
}
