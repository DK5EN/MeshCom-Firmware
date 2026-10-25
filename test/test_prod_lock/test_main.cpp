/**
 * Production-locked settings (src/prod_lock.h, docs/production-locked-settings.md).
 * Every surface: before the lock a locked value was accepted, now it is refused
 * in production (instrument = false) and still accepted with instrument = true.
 */
#include <unity.h>
#include <string.h>
#include <math.h>

#include "prod_lock.h"

struct FakeSettings
{
    int node_sset;
    int node_sset4;
    int node_pingtime;
    int node_pingmax;
    char node_pingcall[10];
    float node_specstart, node_specend, node_specstep;
    int node_specsamples;
    int max_hop_pos;
    int node_postime;
    int node_parm_time;
    int node_stor;
    int node_autoupd;
    int node_updchan;
};

static FakeSettings broken(void)
{
    FakeSettings s;
    memset(&s, 0, sizeof(s));
    s.node_sset = 0x0400 | 0x0004 | 0x8000;
    s.node_sset4 = 0x3B00 | 0x0400 | 0x0002;   /* 0x0400 = --nbrdebug, must survive */
    s.node_pingtime = 20;
    s.node_pingmax = 99;
    s.node_specstart = 863.0f; s.node_specend = 870.0f; s.node_specstep = 0.1f;
    s.node_specsamples = 600;
    s.max_hop_pos = 7;
    s.node_postime = 1;
    s.node_parm_time = 1800;
    s.node_stor = 1;
    s.node_autoupd = 2;
    return s;
}

void setUp(void) {}
void tearDown(void) {}

static void test_gate_refuses_locked_names_in_production(void)
{
    static const char *const cmds[] = {
        "nbrrelay on", "nbrrelay count", "nbrrelay off", "nbrsym off", "nbrreport on", "shortpath on",
        "pingtime 30", "pingmax 3", "pingmax max", "specstart 433.0", "specend 435.0",
        "specstep 0.5", "specsamples 600", "NBRRELAY on", "shortpath"
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i)
    {
        TEST_ASSERT_NOT_NULL_MESSAGE(prodLockRefuses(cmds[i], false), cmds[i]);
        TEST_ASSERT_NULL_MESSAGE(prodLockRefuses(cmds[i], true), cmds[i]);
    }
    TEST_ASSERT_EQUAL_STRING("nbrrelay", prodLockRefuses("nbrrelay on", false));
    TEST_ASSERT_EQUAL_STRING("specsamples", prodLockRefuses("specsamples 600", false));
}

static void test_gate_leaves_other_commands(void)
{
    static const char *const cmds[] = {
        "nbrdebug on", "nbrreset", "maxhop 3", "pingcall DK5EN-1", "postime 600", "ptime 15",
        "txpower 2", "stor off", "stor", "autoupdate notify", "autoupdate", "pingtimeout 5", "spectrum", "info"
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); ++i)
    {
        TEST_ASSERT_NULL_MESSAGE(prodLockRefuses(cmds[i], false), cmds[i]);
        TEST_ASSERT_NULL_MESSAGE(prodLockRefuses(cmds[i], true), cmds[i]);
    }
}

static void test_autoupdate_auto_refused_in_all_builds(void)
{
    TEST_ASSERT_EQUAL_STRING("autoupdate", prodLockRefuses("autoupdate auto", false));
    TEST_ASSERT_EQUAL_STRING("autoupdate", prodLockRefuses("autoupdate auto", true));
    TEST_ASSERT_EQUAL_STRING("autoupdate", prodLockRefuses("autoupdate  AUTO ", true));
    TEST_ASSERT_NULL(prodLockRefuses("autoupdate notify", true));
}

static void test_stor_on_refused_in_production_only(void)
{
    TEST_ASSERT_EQUAL_STRING("stor", prodLockRefuses("stor on", false));
    TEST_ASSERT_EQUAL_STRING("stor", prodLockRefuses("stor  ON", false));
    TEST_ASSERT_NULL(prodLockRefuses("stor on", true));
    TEST_ASSERT_NULL(prodLockRefuses("stor off", false));
}

static void test_normalise_repairs_stored_garbage_in_production(void)
{
    FakeSettings s = broken();
    int n = prodLockNormalise(s, false);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_HEX(0x8004, s.node_sset);            /* shortpath bit gone, foreign bits kept */
    TEST_ASSERT_EQUAL_HEX(0x1C02, s.node_sset4);           /* nbrrelay on (0x1800), report/sym bits gone, --nbrdebug kept */
    TEST_ASSERT_EQUAL_INT(0, s.node_pingtime);             /* no pingcall -> 0 */
    TEST_ASSERT_EQUAL_INT(5, s.node_pingmax);              /* > 5 repaired; a stored 0 stays (equivalent) */
    TEST_ASSERT_EQUAL_FLOAT(432.0f, s.node_specstart);
    TEST_ASSERT_EQUAL_FLOAT(434.0f, s.node_specend);
    TEST_ASSERT_EQUAL_FLOAT(0.025f, s.node_specstep);
    TEST_ASSERT_EQUAL_INT(2048, s.node_specsamples);
    TEST_ASSERT_EQUAL_INT(2, s.max_hop_pos);
    TEST_ASSERT_EQUAL_INT(300, s.node_postime);
    TEST_ASSERT_EQUAL_INT(120, s.node_parm_time);
    TEST_ASSERT_EQUAL_INT(0, s.node_stor);
    TEST_ASSERT_EQUAL_INT(1, s.node_autoupd);
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, false));  /* idempotent */
    s.node_pingmax = 0;
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, false));
    TEST_ASSERT_EQUAL_INT(0, s.node_pingmax);
}

static void test_normalise_pingtime_follows_pingcall(void)
{
    FakeSettings s = broken();
    strcpy(s.node_pingcall, "DK5EN-1");
    prodLockNormalise(s, false);
    TEST_ASSERT_EQUAL_INT(60, s.node_pingtime);
}

static void test_normalise_instrument_keeps_tuning_but_repairs_ranges(void)
{
    FakeSettings s = broken();
    prodLockNormalise(s, true);
    TEST_ASSERT_EQUAL_HEX(0x0400 | 0x0004 | 0x8000, s.node_sset);
    TEST_ASSERT_EQUAL_HEX(0x3B00 | 0x0400 | 0x0002, s.node_sset4);
    TEST_ASSERT_EQUAL_FLOAT(863.0f, s.node_specstart);
    TEST_ASSERT_EQUAL_INT(600, s.node_specsamples);
    TEST_ASSERT_EQUAL_INT(7, s.max_hop_pos);
    TEST_ASSERT_EQUAL_INT(1, s.node_stor);                 /* bench keeps the mailbox announcement */
    /* range repairs and the auto mode apply everywhere */
    TEST_ASSERT_EQUAL_INT(1, s.node_autoupd);
    TEST_ASSERT_EQUAL_INT(300, s.node_postime);
    TEST_ASSERT_EQUAL_INT(120, s.node_parm_time);
    TEST_ASSERT_EQUAL_INT(5, s.node_pingmax);
    TEST_ASSERT_EQUAL_INT(60, s.node_pingtime);            /* 20 is below the loop threshold */
}

/* No MC_AU_DEV_CHANNEL in this env (the upstream view): a stored or imported dev channel becomes prod. */
static void test_normalise_updchan_without_dev_channel(void)
{
    FakeSettings s;
    memset(&s, 0, sizeof(s));
    s.node_parm_time = 15; s.node_autoupd = 1; s.node_pingmax = 5; s.node_sset4 = 0x1800;
    s.node_specstart = 432.0f; s.node_specend = 434.0f; s.node_specstep = 0.025f;
    s.node_specsamples = 2048; s.max_hop_pos = 2;
    s.node_updchan = 1;
#if defined(MC_AU_DEV_CHANNEL)
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, false));
    TEST_ASSERT_EQUAL_INT(1, s.node_updchan);
#else
    TEST_ASSERT_EQUAL_INT(1, prodLockNormalise(s, false));
    TEST_ASSERT_EQUAL_INT(0, s.node_updchan);
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, true));   /* INSTRUMENT too: the channel is a build fact */
#endif
}

static void test_normalise_nbrrelay_on_is_locked_default(void)
{
    FakeSettings s;
    memset(&s, 0, sizeof(s));
    s.node_parm_time = 15; s.node_autoupd = 1; s.node_pingmax = 5;
    s.node_specstart = 432.0f; s.node_specend = 434.0f; s.node_specstep = 0.025f;
    s.node_specsamples = 2048; s.max_hop_pos = 2;
    TEST_ASSERT_EQUAL_INT(1, prodLockNormalise(s, false));  /* only the sset4 field changes */
    TEST_ASSERT_EQUAL_HEX(0x1800, s.node_sset4);
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, false));  /* idempotent */
    s.node_sset4 = 0x3B00;
    TEST_ASSERT_EQUAL_INT(1, prodLockNormalise(s, false));
    TEST_ASSERT_EQUAL_HEX(0x1800, s.node_sset4);
    s.node_sset4 = 0x0400 | 0x0002;                         /* --nbrdebug and a foreign bit survive */
    prodLockNormalise(s, false);
    TEST_ASSERT_EQUAL_HEX(0x1C02, s.node_sset4);
    /* INSTRUMENT: bits as stored */
    s.node_sset4 = 0;
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, true));
    TEST_ASSERT_EQUAL_HEX(0, s.node_sset4);
    s.node_sset4 = 0x3B00;
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, true));
    TEST_ASSERT_EQUAL_HEX(0x3B00, s.node_sset4);
}

static void test_normalise_nrf52_leaves_autoupd(void)
{
    FakeSettings s = broken();
    s.node_autoupd = 0;
    prodLockNormalise(s, false, false);
    TEST_ASSERT_EQUAL_INT(0, s.node_autoupd);
    s.node_autoupd = 2;
    prodLockNormalise(s, false, false);
    TEST_ASSERT_EQUAL_INT(2, s.node_autoupd);
}

static void test_normalise_keeps_valid_values(void)
{
    FakeSettings s;
    memset(&s, 0, sizeof(s));
    s.node_postime = 0;            /* 0 = default interval stays */
    s.node_parm_time = 15;
    s.node_autoupd = 1;
    s.node_specstart = 432.0f; s.node_specend = 434.0f; s.node_specstep = 0.025f;
    s.node_specsamples = 2048; s.max_hop_pos = 2; s.node_pingmax = 5;
    s.node_sset4 = 0x1800;         /* locked production default: --nbrrelay on */
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, false));
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, true));
    s.node_postime = 86400;
    TEST_ASSERT_EQUAL_INT(0, prodLockNormalise(s, false));
}

static void test_set_hop_clamp(void)
{
    /* production: text 1..6 only, position field never */
    TEST_ASSERT_EQUAL_INT(4, prodLockSetHop(9, 4, false));
    TEST_ASSERT_EQUAL_INT(4, prodLockSetHop(0, 4, false));
    TEST_ASSERT_EQUAL_INT(4, prodLockSetHop(7, 4, false));
    TEST_ASSERT_EQUAL_INT(3, prodLockSetHop(3, 4, false));
    TEST_ASSERT_EQUAL_INT(6, prodLockSetHop(6, 4, false));
    TEST_ASSERT_FALSE(prodLockSetPosAllowed(false));
    /* INSTRUMENT: as before, 0..7 */
    TEST_ASSERT_EQUAL_INT(0, prodLockSetHop(0, 4, true));
    TEST_ASSERT_EQUAL_INT(7, prodLockSetHop(7, 4, true));
    TEST_ASSERT_EQUAL_INT(4, prodLockSetHop(9, 4, true));
    TEST_ASSERT_TRUE(prodLockSetPosAllowed(true));
}

static void test_bug_clamps(void)
{
    TEST_ASSERT_EQUAL_INT(0, prodClampPostime(0));        /* 0 = default 1800, no longer 300 */
    TEST_ASSERT_EQUAL_INT(300, prodClampPostime(1));
    TEST_ASSERT_EQUAL_INT(300, prodClampPostime(299));
    TEST_ASSERT_EQUAL_INT(300, prodClampPostime(300));
    TEST_ASSERT_EQUAL_INT(86400, prodClampPostime(86400));
    TEST_ASSERT_EQUAL_INT(86400, prodClampPostime(86401));
    TEST_ASSERT_EQUAL_INT(5, prodClampPtime(4));
    TEST_ASSERT_EQUAL_INT(5, prodClampPtime(5));
    TEST_ASSERT_EQUAL_INT(120, prodClampPtime(120));
    TEST_ASSERT_EQUAL_INT(120, prodClampPtime(121));
    TEST_ASSERT_EQUAL_INT(120, prodClampPtime(200));      /* was 1800 (30 hours) */
    TEST_ASSERT_FALSE(prodLockPingtimeValid(15));         /* loop pings only above 29 */
    TEST_ASSERT_FALSE(prodLockPingtimeValid(29));
    TEST_ASSERT_TRUE(prodLockPingtimeValid(30));
    TEST_ASSERT_TRUE(prodLockPingtimeValid(300));
    TEST_ASSERT_FALSE(prodLockPingtimeValid(301));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_gate_refuses_locked_names_in_production);
    RUN_TEST(test_gate_leaves_other_commands);
    RUN_TEST(test_autoupdate_auto_refused_in_all_builds);
    RUN_TEST(test_stor_on_refused_in_production_only);
    RUN_TEST(test_normalise_repairs_stored_garbage_in_production);
    RUN_TEST(test_normalise_pingtime_follows_pingcall);
    RUN_TEST(test_normalise_instrument_keeps_tuning_but_repairs_ranges);
    RUN_TEST(test_normalise_nbrrelay_on_is_locked_default);
    RUN_TEST(test_normalise_nrf52_leaves_autoupd);
    RUN_TEST(test_normalise_keeps_valid_values);
    RUN_TEST(test_set_hop_clamp);
    RUN_TEST(test_bug_clamps);
    RUN_TEST(test_normalise_updchan_without_dev_channel);
    return UNITY_END();
}
